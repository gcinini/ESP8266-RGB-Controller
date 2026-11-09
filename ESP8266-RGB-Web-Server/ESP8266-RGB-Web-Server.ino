/*
  ESP8266 RGB Controller  v4.1.0

  Wi-Fi controller for a WS2812B LED lamp on a Wemos D1 Mini (ESP8266).
  Control it from its own web page, by voice through an Amazon Echo (Alexa),
  or from Home Assistant (over MQTT, with automatic discovery).

  Copyright (c) 2019-2026 G Cinini
  SPDX-License-Identifier: MIT
  See LICENSE, and THIRD_PARTY_NOTICES.md for the libraries this sketch uses.

  ==========================================================================
  HOW TO READ THIS FILE
  ==========================================================================
  Arduino sketches are written in C++, a bigger language built on top of C.
  Most of this file is plain C (variables, functions, if/for/switch, structs);
  where it uses something that is C++ only (classes such as String, lambdas,
  references) the comment says so.

  Every Arduino program has two special functions:
    setup()  runs once, when the board powers up or resets
    loop()   runs over and over, forever, after setup() finishes

  The sketch is organised in this order - you can read it top to bottom:
    1. Libraries and settings            (#include lines, config.h)
    2. Lamp state                         (the variables that describe the lamp)
    3. setup() and loop()
    4. Lamp control                       (functions that change the state)
    5. Colours                            (turning "Hot Pink" or "#FF8800" into RGB)
    6. Drawing                            (turning the state into LED colours, fades, effects)
    7. Saving settings                    (so the lamp remembers after a power cut)
    8. Wi-Fi, OTA and Home Assistant     (networking)
    9. Alexa                              (voice control through an Echo)
   10. Web interface                      (the web page and its small API)

  THE BIG IDEA
  ------------
  A microcontroller can only do one thing at a time. If the code waited
  (with delay()) for an animation to finish, the lamp could not answer the
  web page or Home Assistant in the meantime.

  So nothing here ever waits for long. Instead:
    * web requests, Alexa commands and MQTT messages only CHANGE THE STATE
      (the `lamp` variable) and return straight away;
    * loop() then draws ONE animation frame from that state, and comes back
      around a few microseconds later to check the network again.
  This is called "non-blocking" code, and it is the most important pattern in
  embedded programming.

  WHERE THE SETTINGS ARE
  ----------------------
    config.h    LED count, pin, names, MQTT broker... (one block per lamp)
    secrets.h   Wi-Fi and MQTT broker passwords (not on GitHub)
*/


// ===========================================================================
// 1. Libraries and settings
// ===========================================================================
// #include copies another file into this one before compiling. Files in
// <angle brackets> come from installed libraries; files in "quotes" are in
// this sketch's own folder.

#include <ESP8266WiFi.h>        // joining a Wi-Fi network
#include <ESP8266WebServer.h>   // serving the web page
#include <ArduinoOTA.h>         // receiving firmware updates over Wi-Fi
#include <EEPROM.h>             // saving settings so they survive a power cut
#include <PubSubClient.h>       // MQTT, the messaging system Home Assistant listens on
#include <ArduinoJson.h>        // reading and writing JSON, the text format Home Assistant uses

// Espalexa makes the lamp look like a Philips Hue bulb to an Amazon Echo.
// It keeps a table of devices; this lamp is one device, so reserve room for
// just one (the default of 10 would waste memory). This #define must come
// before the #include, because the library reads it while it is included.
#define ESPALEXA_MAXDEVICES 1
#include <Espalexa.h>

// FastLED needs this setting *before* it is included: it makes pin names like
// D4 mean the same thing to FastLED as they do to the rest of the sketch.
#define FASTLED_ESP8266_RAW_PIN_ORDER
#include <FastLED.h>            // driving the LED strip

// secrets.h is included before config.h, so secrets.h can override settings
// such as ENABLE_ALEXA. __has_include checks whether the file exists, so a
// missing secrets.h gives a clear message instead of a confusing error.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found: copy secrets.example.h to secrets.h and fill in your credentials"
#endif

// `defined(X)` is true if X has been #defined. Version 3 used MQTT_KEY for the
// Adafruit IO key; version 4 needs a login for your own MQTT broker instead.
#if !defined(MQTT_USER) || !defined(MQTT_PASSWORD)
#error "secrets.h needs MQTT_USER and MQTT_PASSWORD for your MQTT broker (see secrets.example.h)"
#endif

#include "config.h"             // this lamp's settings
#include "web_page.h"           // the web page (HTML, CSS and JavaScript as one text)

// A `const` variable can't be changed after it is created. The compiler
// stops you if you try. `const char *` is a pointer to text: in C, text is
// stored as a row of characters, and the variable holds where it starts.
const char *FIRMWARE_VERSION = "4.1.0";


// ===========================================================================
// 2. Lamp state
// ===========================================================================
// Integer types used throughout:
//   uint8_t   whole number 0-255           (u = unsigned = never negative, 8 bits)
//   uint16_t  whole number 0-65535         (16 bits)
//   uint32_t  whole number 0-4294967295    (32 bits)
// Using the smallest type that fits saves memory, which matters on a chip
// with about 80 KB of RAM.

// --- Modes -----------------------------------------------------------------
// Each mode has a number. The web page sends these numbers (/mode?id=3) and
// they are what gets saved in flash, so don't renumber them.
const uint8_t MODE_SOLID           = 1;   // one colour
const uint8_t MODE_FADE            = 2;   // whole lamp slowly drifts through colours
const uint8_t MODE_RAINBOW         = 3;   // rainbow moving along the strip
const uint8_t MODE_RANDOM          = 4;   // a different palette every few seconds
const uint8_t MODE_PALETTE_SMOOTH  = 5;   // chosen palette, blended
const uint8_t MODE_PALETTE_STEPPED = 6;   // chosen palette, hard steps
const uint8_t MODE_CANDLE          = 7;   // flickering candle at the bottom of the lamp
// Added in v4.1:
const uint8_t MODE_FIREPLACE       = 8;   // glowing embers along the whole bar
const uint8_t MODE_BREATHE         = 9;   // the chosen colour slowly brightens and dims
const uint8_t MODE_TWINKLE         = 10;  // stars twinkling on a dim background
const uint8_t MODE_COMET           = 11;  // a bright dot with a fading tail, bouncing end to end
const uint8_t MODE_READING         = 12;  // warm white, bright in the middle and dim at the ends
const uint8_t MODE_SUNRISE         = 13;  // wake-up: dark red to warm white over SUNRISE_MINUTES
const uint8_t MODE_NIGHT           = 14;  // very dim amber night light
const uint8_t MODE_MIN = MODE_SOLID;
const uint8_t MODE_MAX = MODE_NIGHT;

// An array is a numbered list. MODE_NAMES[3] is "Rainbow". Index 0 is
// unused, so the index matches the mode number. `const char *const` means
// neither the list nor the text in it can be changed.
// Home Assistant shows these names in its Effect list, and sends one back to
// choose a mode, so changing a name here changes it there too.
const char *const MODE_NAMES[] = {
  "", "Solid colour", "Colour fade", "Rainbow", "Random", "Palette, smooth", "Palette, stepped", "Candle",
  "Fireplace", "Breathe", "Twinkle", "Comet", "Reading light", "Sunrise", "Night light"
};

// Effect speed 1-10 becomes "how far the animation clock moves each frame".
// 256 (speed 5) is one step of animationPhase per frame, the speed of v4.0.
const uint16_t SPEED_STEPS[] = { 0, 64, 96, 128, 176, 256, 352, 448, 576, 768, 1024 };
const uint8_t  SPEED_MIN = 1, SPEED_MAX = 10;

// --- Palettes -----------------------------------------------------------------
// A palette is a list of 16 colours that FastLED blends between. CloudColors_p
// and friends are built into FastLED. The `&` takes the *address* of each
// palette, so the table stores where the palette is rather than a copy of it.
// A struct groups several values into one new type, like a row in a table.
struct NamedPalette {
  const char *name;
  const TProgmemRGBPalette16 *colours;   // `*` = pointer to (address of) a palette
};

const NamedPalette PALETTES[] = {
  { "Cloud",  &CloudColors_p  },
  { "Ocean",  &OceanColors_p  },
  { "Party",  &PartyColors_p  },
  { "Heat",   &HeatColors_p   },
  { "Forest", &ForestColors_p },
  { "Lava",   &LavaColors_p   },
};
// sizeof gives a size in bytes. Total size divided by the size of one entry
// = number of entries, so this stays right if you add or remove a palette.
const uint8_t PALETTE_COUNT = sizeof(PALETTES) / sizeof(PALETTES[0]);

// --- The lamp itself ----------------------------------------------------------
// Everything that describes what the lamp is showing, in one struct.
// CRGB is FastLED's colour type: three uint8_t values named r, g and b.
struct Lamp {
  bool    on;           // true or false
  uint8_t mode;         // one of the MODE_ numbers
  uint8_t palette;      // index into PALETTES
  CRGB    colour;       // the colour as picked (gamma is applied when drawing)
  uint8_t brightness;   // 1-255, applied through FastLED.setBrightness()
  uint8_t speed;        // effect speed, SPEED_MIN-SPEED_MAX
  uint16_t kelvin;      // white temperature if one was chosen (e.g. 2700), or 0 for a picked RGB colour
};

// The one and only lamp, with its starting values (from config.h).
Lamp lamp = { true, MODE_SOLID, 0, CRGB(DEFAULT_RED, DEFAULT_GREEN, DEFAULT_BLUE), DEFAULT_BRIGHTNESS,
              DEFAULT_SPEED, 0 };


// --- Other global variables -----------------------------------------------------
// "Global" means declared outside any function, so every function can use it.
CRGB          leds[LED_COUNT];        // one colour per LED; FastLED sends this array to the strip
CRGBPalette16 activePalette;          // the palette the current mode is drawing with
TBlendType    activeBlend = LINEARBLEND;   // LINEARBLEND = smooth, NOBLEND = hard steps
CRGB          solidColour;            // lamp.colour after gamma correction (see lampChanged)
uint32_t      animationClock = 0;     // grows every frame by SPEED_STEPS[lamp.speed]
uint8_t       animationPhase = 0;     // animationClock / 256: how far an animation has moved, wraps 255 -> 0

// --- Fades ------------------------------------------------------------------------
// Changes don't jump: the brightness actually shown (outputLevel) and the solid
// colour actually shown (colourShown) glide to their new values over fadeLength
// milliseconds. lampChanged() starts a fade; drawFrame() moves it along.
uint8_t  outputLevel = 0;             // brightness on the strip right now, 0-255 (0 = dark)
uint8_t  fadeFrom = 0, fadeTo = 0;    // brightness at the start and end of the fade
CRGB     colourShown;                 // solid colour on the strip right now
CRGB     colourFrom;                  // solid colour at the start of the fade
uint32_t fadeStartAt = 0;             // millis() when the fade started
uint32_t fadeLength = 0;              // how long the fade lasts, in ms
// How long the *next* change should fade. Home Assistant can set it for one
// command (its "transition"); everything else uses DEFAULT_FADE_MS.
uint32_t requestedFadeMs = DEFAULT_FADE_MS;
bool     transitionGiven = false;     // true while handling a command that named a transition

// --- Timers for Sunrise and the sleep timer ---------------------------------------
uint32_t sunriseStartAt = 0;          // millis() when Sunrise started
uint32_t sunriseLength = 0;           // how long it lasts, in ms
uint32_t sleepStartAt = 0;            // millis() when the sleep timer was set
uint32_t sleepLength = 0;             // 0 = no sleep timer running

// These are *objects*, a C++ idea: a variable that carries its own functions
// (called methods), used like web.send(...) or mqtt.connect(...).
ESP8266WebServer web(80);             // web server on port 80, the normal HTTP port
WiFiClient       mqttSocket;          // a network connection (TCP) for MQTT to use
PubSubClient     mqtt(mqttSocket);    // MQTT client that talks over mqttSocket

#if ENABLE_ALEXA
Espalexa         espalexa;            // answers the Echo's searches and commands
// A pointer to the "bulb" Alexa sees. nullptr means "points to nothing yet";
// startAlexa() creates the device and stores its address here.
EspalexaDevice  *alexaLight = nullptr;
#endif

// MQTT "topics" - the addresses messages are sent to - built in setup()
// from MQTT_TOPIC, e.g. "bedlight/state". String is a C++ class for text
// that can grow; it is easier than C text but uses more memory, so this
// sketch uses it only where it helps.
String topicState;          // <topic>/state         lamp -> Home Assistant: what the lamp is doing
String topicSet;            // <topic>/set           Home Assistant -> lamp: light commands
String topicPaletteSet;     // <topic>/palette/set   Home Assistant -> lamp: choose a palette
String topicSpeedSet;       // <topic>/speed/set     Home Assistant -> lamp: effect speed 1-10
String topicSleepSet;       // <topic>/sleep/set     Home Assistant -> lamp: sleep timer in minutes (0 = off)
String topicAvailability;   // <topic>/availability  "online" or "offline"
// Two pieces of text written side by side are joined into one by the
// compiler, so this is "homeassistant/status". Home Assistant says "online"
// there whenever it starts.
const char *HA_STATUS_TOPIC = HA_DISCOVERY_PREFIX "/status";

// A name unique to this lamp's chip, e.g. "bedlight_a1b2c3", so Home
// Assistant can tell it apart from every other device. Filled in by setup().
char deviceId[32];          // a C text buffer: room for 31 characters plus the end marker

// Bookkeeping for the delayed jobs (saving, reports to Home Assistant).
bool     savePending = false;       // true = settings changed and need saving
uint32_t lastChangeAt = 0;          // millis() when the lamp last changed
bool     statePending = true;       // true = Home Assistant should get a state report
uint32_t lastStateAt = 0;           // millis() of the last report (0 = never)
bool     discoveryPending = false;  // true = announce the lamp to Home Assistant


// ===========================================================================
// Function declarations ("prototypes")
// ===========================================================================
// In C, a function must be declared before it is used. These lines tell the
// compiler each function's name, inputs and result, so the functions can be
// written in any order below. `void` means "returns nothing".

// Lamp control (used by the web page, Alexa and Home Assistant)
void setPower(bool on);
void setMode(uint8_t mode);
void setPalette(uint8_t id);
void setColour(CRGB colour);
void setBrightness(uint8_t level);
void setColourTemperature(uint16_t kelvin);
void setSpeed(uint8_t speed);
void setSleepTimer(uint16_t minutes);
void serviceSleepTimer();
uint16_t sleepMinutesLeft();
void lampChanged();
void preparePalette();
bool colourFromText(String text, CRGB &out);
CRGB kelvinToRgb(uint16_t kelvin);

// Drawing
void drawFrame();
void updateFade(uint32_t now);
uint16_t bySpeed(uint16_t amount);
void drawPaletteAlongStrip();
void drawCandle();
void drawFireplace();
void drawBreathe();
void drawTwinkle();
void drawComet();
void drawReadingLight();
void drawSunrise(uint32_t now);
void pickRandomPalette();

// Status
String uptimeText();

// Settings in flash
void loadSettings();
void saveSettingsIfDue();

// Networking
void connectWiFi();
void startOta();
void serviceMqtt();
void onMqttMessage(char *topic, byte *payload, unsigned int length);
void handleLightCommand(byte *payload, unsigned int length);
void applyLightCommand(JsonDocument &command);
long payloadToNumber(byte *payload, unsigned int length);
bool publishJson(const String &topic, JsonDocument &doc);
void addDeviceInfo(JsonDocument &doc);
void publishDiscovery();
void publishState();
void publishToHomeAssistantIfDue();

// Alexa
void startAlexa();
void onAlexaCommand(EspalexaDevice *device);
void syncAlexa();

// Web interface
void startWebServer();
void replyWithState();
void replyBadRequest(const char *reason);
String stateAsJson();


// ===========================================================================
// 3. setup() and loop()
// ===========================================================================
void setup() {
  // The serial port sends text back to your computer over USB. Open the
  // serial monitor at 115200 baud to see these messages.
  Serial.begin(115200);
  Serial.println();
  Serial.printf("ESP8266 RGB Controller v%s - lamp %d (%s)\n", FIRMWARE_VERSION, LAMP_ID, DEVICE_NAME);

  // Tell FastLED what kind of strip we have and where it is. The values in
  // <angle brackets> are template parameters (a C++ feature): they must be
  // known when compiling, which is why they come from #define in config.h.
  FastLED.addLeds<LED_TYPE, LED_PIN, LED_ORDER>(leds, LED_COUNT).setCorrection(TypicalLEDStrip);
  FastLED.setDither(1);   // smoother colours at low brightness
  if (MAX_MILLIAMPS > 0) {
    FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);   // stay within the power supply's limit
  }

  loadSettings();   // restore the last state, so the lamp lights up straight away
  connectWiFi();    // waits for Wi-Fi, but keeps animating while it waits
  startOta();
  startWebServer();

  // ESP.getChipId() is a number burned into every ESP8266 chip. %06x prints
  // it as 6 hexadecimal digits, so deviceId becomes e.g. "bedlight_a1b2c3".
  snprintf(deviceId, sizeof(deviceId), "%s_%06x", MQTT_TOPIC, ESP.getChipId());

  // Build the MQTT topics. Adding Strings with + joins them together.
  topicState        = String(MQTT_TOPIC) + "/state";
  topicSet          = String(MQTT_TOPIC) + "/set";
  topicPaletteSet   = String(MQTT_TOPIC) + "/palette/set";
  topicSpeedSet     = String(MQTT_TOPIC) + "/speed/set";
  topicSleepSet     = String(MQTT_TOPIC) + "/sleep/set";
  topicAvailability = String(MQTT_TOPIC) + "/availability";
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);   // "call onMqttMessage() whenever a message arrives"
  mqtt.setBufferSize(512);           // largest message we can receive (Home Assistant's commands are small)
}

// Each of these returns almost immediately. That's what keeps the lamp
// responsive: loop() runs many thousands of times a second.
void loop() {
  ArduinoOTA.handle();     // is someone sending a firmware update?
#if ENABLE_ALEXA
  espalexa.loop();         // web page requests AND the Echo's searches and commands
#else
  web.handleClient();      // has the web page sent a request?
#endif
  serviceMqtt();           // any Home Assistant messages? reconnect if needed
  drawFrame();             // draw the next animation frame, if it's time
  saveSettingsIfDue();     // save settings, if it's time
  serviceSleepTimer();     // switch off if the sleep timer has run out
  publishToHomeAssistantIfDue();   // announce the lamp or report its state, if it's time
}


// ===========================================================================
// 4. Lamp control
// ===========================================================================
// Every change to the lamp goes through one of these functions, whether it
// came from the web page, Alexa or Home Assistant. Each one updates `lamp`, prints
// a line to the serial monitor, and calls lampChanged().

void setPower(bool on) {
  lamp.on = on;
  if (!on) sleepLength = 0;   // switching off cancels the sleep timer
  // `condition ? a : b` is the ternary operator: a if true, b if false.
  Serial.println(on ? "Lamp on" : "Lamp off");
  lampChanged();
}

void setMode(uint8_t mode) {
  if (mode < MODE_MIN || mode > MODE_MAX) return;   // || means "or"; ignore unknown modes
  lamp.mode = mode;
  lamp.on = true;
  if (mode == MODE_SUNRISE) {
    // Sunrise lasts SUNRISE_MINUTES, unless Home Assistant sent a transition
    // with the command: then that is the length ("transition: 1800" = 30 min).
    // UL makes the numbers "unsigned long", so 20 * 60000 can't overflow.
    sunriseLength = (transitionGiven && requestedFadeMs > 0) ? requestedFadeMs
                                                             : SUNRISE_MINUTES * 60000UL;
    sunriseStartAt = millis();
    Serial.printf("Mode Sunrise over %lu min\n", (unsigned long)(sunriseLength / 60000));
  } else {
    Serial.printf("Mode %s\n", MODE_NAMES[mode]);   // %s in printf is replaced by text
  }
  lampChanged();
}

void setPalette(uint8_t id) {
  if (id >= PALETTE_COUNT) return;
  lamp.palette = id;
  // Show the palette straight away. != means "is not equal to".
  // If "stepped" is already showing, keep it; otherwise use "smooth".
  if (lamp.mode != MODE_PALETTE_STEPPED) lamp.mode = MODE_PALETTE_SMOOTH;
  lamp.on = true;
  Serial.printf("Palette %s\n", PALETTES[id].name);   // . reads a field of a struct
  lampChanged();
}

void setColour(CRGB colour) {
  lamp.colour = colour;
  lamp.kelvin = 0;          // an RGB colour, not a white temperature
  lamp.mode = MODE_SOLID;
  lamp.on = true;
  // %02X prints a number as 2 hexadecimal digits, so 255 becomes FF.
  Serial.printf("Colour #%02X%02X%02X\n", colour.r, colour.g, colour.b);
  lampChanged();
}

// A white of a given colour temperature, e.g. 2700 (warm) or 6500 (daylight).
// It is shown like any solid colour, but the lamp remembers the kelvin value
// so Home Assistant's colour-temperature picker shows it correctly.
void setColourTemperature(uint16_t kelvin) {
  kelvin = constrain(kelvin, MIN_KELVIN, MAX_KELVIN);
  lamp.colour = kelvinToRgb(kelvin);
  lamp.kelvin = kelvin;
  lamp.mode = MODE_SOLID;
  lamp.on = true;
  Serial.printf("White %u K\n", kelvin);
  lampChanged();
}

void setBrightness(uint8_t level) {
  lamp.brightness = (level == 0) ? 1 : level;   // never 0, so "on" always shows something
  Serial.printf("Brightness %u\n", lamp.brightness);   // %u = unsigned whole number
  lampChanged();
}

void setSpeed(uint8_t speed) {
  lamp.speed = constrain(speed, SPEED_MIN, SPEED_MAX);
  Serial.printf("Speed %u\n", lamp.speed);
  lampChanged();
}

// Sleep timer: switch off (with a slow fade) after `minutes`. 0 cancels it.
// It isn't saved in flash: after a power cut there's no timer running.
void setSleepTimer(uint16_t minutes) {
  if (minutes > SLEEP_MAX_MINUTES) minutes = SLEEP_MAX_MINUTES;
  sleepLength = minutes * 60000UL;
  sleepStartAt = millis();
  if (minutes == 0) Serial.println("Sleep timer off");
  else              Serial.printf("Sleep timer: off in %u min\n", minutes);
  statePending = true;           // tell Home Assistant (no need to save anything)
  lastChangeAt = millis();
}

// Called from loop(): has the sleep timer run out?
void serviceSleepTimer() {
  if (sleepLength == 0) return;                          // no timer running
  if (millis() - sleepStartAt < sleepLength) return;     // not yet
  sleepLength = 0;
  Serial.println("Sleep timer finished");
  requestedFadeMs = SLEEP_FADE_SECONDS * 1000UL;         // fade off slowly...
  setPower(false);
  requestedFadeMs = DEFAULT_FADE_MS;                     // ...then back to normal fades
}

// Whole minutes left on the sleep timer, rounded up (0 = no timer).
uint16_t sleepMinutesLeft() {
  if (sleepLength == 0) return 0;
  uint32_t elapsed = millis() - sleepStartAt;
  if (elapsed >= sleepLength) return 0;
  return (sleepLength - elapsed + 59999) / 60000;        // + 59999 rounds up
}

// Called after every change: applies it and schedules the delayed jobs.
void lampChanged() {
  // Gamma correction. Our eyes don't see brightness in a straight line: an
  // LED at half power looks much brighter than "half". applyGamma_video()
  // bends the numbers so colours picked on a screen look right on the LEDs.
  solidColour = CRGB(applyGamma_video(lamp.colour.r, COLOUR_GAMMA),
                     applyGamma_video(lamp.colour.g, COLOUR_GAMMA),
                     applyGamma_video(lamp.colour.b, COLOUR_GAMMA));

  // Start a fade from what is showing now to the new brightness and colour.
  // Off fades to 0; Night light always uses its own very low level.
  fadeFrom = outputLevel;
  if (!lamp.on)                       fadeTo = 0;
  else if (lamp.mode == MODE_NIGHT)   fadeTo = NIGHT_LIGHT_LEVEL;
  else                                fadeTo = lamp.brightness;
  colourFrom = colourShown;
  fadeStartAt = millis();
  // Sunrise does its own slow ramp, so the fade into it is a normal short one.
  fadeLength = (lamp.mode == MODE_SUNRISE) ? DEFAULT_FADE_MS : requestedFadeMs;

  preparePalette();
  syncAlexa();   // so the Alexa app shows what the lamp is really doing

  // Don't save or report right now. Just note that it needs doing; loop()
  // does it a moment later (see saveSettingsIfDue and publishToHomeAssistantIfDue).
  savePending = true;
  statePending = true;
  lastChangeAt = millis();   // millis() = milliseconds since the board started
}

// Loads the palette and blending the current mode draws with.
// switch picks one `case` by value. `break` jumps to the end of the switch;
// without it, the code would carry on into the next case. Two case labels
// in a row (5 and 6 below) share the same code.
void preparePalette() {
  switch (lamp.mode) {
    case MODE_FADE:
      activePalette = PartyColors_p;
      activeBlend = LINEARBLEND;
      break;
    case MODE_RAINBOW:
      activePalette = RainbowColors_p;
      activeBlend = LINEARBLEND;
      break;
    case MODE_RANDOM:
      pickRandomPalette();
      break;
    case MODE_PALETTE_SMOOTH:
    case MODE_PALETTE_STEPPED:
      // *pointer means "the thing the pointer points to": the palette itself.
      activePalette = *PALETTES[lamp.palette].colours;
      activeBlend = (lamp.mode == MODE_PALETTE_SMOOTH) ? LINEARBLEND : NOBLEND;
      break;
    default:
      break;   // solid colour and candle don't use a palette
  }
}


// ===========================================================================
// 5. Colours: names and hex values
// ===========================================================================
struct NamedColour {
  const char *name;     // lower case, no spaces
  uint8_t r, g, b;
};

// Standard web colour names, plus a warm white that suits LED lamps.
const NamedColour COLOUR_NAMES[] = {
  { "white", 255, 255, 255 },    { "warmwhite", 255, 180, 107 }, { "red", 255, 0, 0 },
  { "green", 0, 128, 0 },        { "lime", 0, 255, 0 },          { "blue", 0, 0, 255 },
  { "navy", 0, 0, 128 },         { "royalblue", 65, 105, 225 },  { "skyblue", 135, 206, 235 },
  { "cyan", 0, 255, 255 },       { "aqua", 0, 255, 255 },        { "teal", 0, 128, 128 },
  { "yellow", 255, 255, 0 },     { "gold", 255, 215, 0 },        { "orange", 255, 165, 0 },
  { "coral", 255, 127, 80 },     { "chocolate", 210, 105, 30 },  { "brown", 165, 42, 42 },
  { "maroon", 128, 0, 0 },       { "olive", 128, 128, 0 },       { "khaki", 240, 230, 140 },
  { "pink", 255, 192, 203 },     { "hotpink", 255, 105, 180 },   { "magenta", 255, 0, 255 },
  { "violet", 238, 130, 238 },   { "plum", 221, 160, 221 },      { "lavender", 230, 230, 250 },
  { "purple", 128, 0, 128 },     { "indigo", 75, 0, 130 },       { "silver", 192, 192, 192 },
};

// Turns text into a colour. Understands names ("Hot Pink", "royalblue") and
// hex ("#FF8800" or "ff8800"). Returns true if it understood the text.
//
// `CRGB &out` is a *reference* (C++): the function writes straight into the
// caller's variable, which is how it can hand back a colour as well as
// returning true/false.
bool colourFromText(String text, CRGB &out) {
  text.trim();             // remove spaces at the start and end
  text.toLowerCase();
  text.replace(" ", "");   // "Hot Pink" -> "hotpink"

  // "for each colour c in COLOUR_NAMES" (a C++ range-based for loop)
  for (const NamedColour &c : COLOUR_NAMES) {
    if (text == c.name) {
      out = CRGB(c.r, c.g, c.b);
      return true;          // found it: stop here
    }
  }

  // Not a name, so try hex: exactly 6 characters, each 0-9 or a-f.
  if (text.startsWith("#")) text.remove(0, 1);   // drop a leading #
  if (text.length() != 6) return false;
  for (unsigned int i = 0; i < text.length(); i++) {   // i = 0, 1, 2 ... 5
    if (!isxdigit(text[i])) return false;               // ! means "not"
  }

  // strtoul turns the text into one number, e.g. "ff8800" -> 0xFF8800.
  // Then pick out each colour's 8 bits:
  //   >> 16  shifts the number right by 16 bits, leaving the red part at the bottom
  //   & 0xFF keeps only the lowest 8 bits (0-255)
  uint32_t value = strtoul(text.c_str(), nullptr, 16);
  out = CRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
  return true;
}

// Turns a white colour temperature (kelvin) into red, green and blue.
// The table holds the colour of a glowing "black body" every 500 K; values in
// between are blended from the two nearest rows. Lower = warmer (more orange),
// higher = cooler (more blue).
CRGB kelvinToRgb(uint16_t kelvin) {
  static const uint8_t TABLE[][3] = {
    { 255, 137,  14 },   // 2000 K  candle
    { 255, 161,  72 },   // 2500 K
    { 255, 180, 107 },   // 3000 K  warm white bulb
    { 255, 196, 137 },   // 3500 K
    { 255, 209, 163 },   // 4000 K  neutral white
    { 255, 219, 186 },   // 4500 K
    { 255, 228, 206 },   // 5000 K
    { 255, 236, 224 },   // 5500 K
    { 255, 243, 239 },   // 6000 K
    { 255, 249, 253 },   // 6500 K  daylight
  };
  kelvin = constrain(kelvin, 2000, 6500);
  uint16_t offset = kelvin - 2000;     // 0-4500
  uint8_t row = offset / 500;          // which table row is just below
  if (row >= 9) return CRGB(TABLE[9][0], TABLE[9][1], TABLE[9][2]);
  // How far between this row and the next, as 0-255 (blend() wants 0-255).
  uint8_t between = (offset % 500) * 255 / 500;
  CRGB low(TABLE[row][0], TABLE[row][1], TABLE[row][2]);
  CRGB high(TABLE[row + 1][0], TABLE[row + 1][1], TABLE[row + 1][2]);
  return blend(low, high, between);
}


// ===========================================================================
// 6. Drawing - one frame per call, at most every FRAME_INTERVAL_MS
// ===========================================================================
// A `static` variable inside a function keeps its value between calls
// (normal local variables start fresh every time). That's how drawFrame()
// remembers when it last drew a frame.
void drawFrame() {
  static uint32_t lastFrameAt = 0;
  static uint32_t randomSince = 0;

  // The non-blocking timer: "has FRAME_INTERVAL_MS passed since the last
  // frame?" If not, return immediately and let loop() do other work.
  // Writing it as `now - lastFrameAt` (not `now > lastFrameAt + interval`)
  // keeps it correct even when millis() rolls over to 0 after 49.7 days,
  // because unsigned subtraction wraps around too.
  uint32_t now = millis();
  if (now - lastFrameAt < FRAME_INTERVAL_MS) return;
  lastFrameAt = now;

  updateFade(now);   // move any fade along, and set the strip's brightness

  // Off, and finished fading out: nothing to draw.
  if (!lamp.on && outputLevel == 0) {
    FastLED.clear();   // all LEDs black...
    FastLED.show();    // ...and send that to the strip
    return;
  }

  // The animation clock. At speed 5 it adds 256 per frame, so animationPhase
  // (the clock divided by 256) goes up by exactly 1 per frame; slower speeds
  // add less, faster speeds more. >> 8 shifts the bits right by 8, which is
  // a quick way to divide by 256.
  animationClock += SPEED_STEPS[lamp.speed];
  animationPhase = animationClock >> 8;

  switch (lamp.mode) {
    case MODE_SOLID:
      fill_solid(leds, LED_COUNT, colourShown);
      break;

    case MODE_FADE:
      // One colour for the whole lamp, drifting a quarter as fast as the
      // other animations (>> 10 = divide by 1024): about 20 s per cycle.
      fill_solid(leds, LED_COUNT, ColorFromPalette(activePalette, animationClock >> 10, 255, activeBlend));
      break;

    case MODE_RANDOM:
      if (now - randomSince >= RANDOM_HOLD_MS) {   // time for a new palette?
        randomSince = now;
        pickRandomPalette();
      }
      drawPaletteAlongStrip();
      break;

    case MODE_CANDLE:    drawCandle();          break;
    case MODE_FIREPLACE: drawFireplace();       break;
    case MODE_BREATHE:   drawBreathe();         break;
    case MODE_TWINKLE:   drawTwinkle();         break;
    case MODE_COMET:     drawComet();           break;
    case MODE_READING:   drawReadingLight();    break;
    case MODE_SUNRISE:   drawSunrise(now);      break;

    case MODE_NIGHT:
      // The colour is fixed; the very low brightness comes from lampChanged().
      fill_solid(leds, LED_COUNT, CRGB(NIGHT_LIGHT_RED, NIGHT_LIGHT_GREEN, NIGHT_LIGHT_BLUE));
      break;

    default:   // rainbow and the two palette modes
      drawPaletteAlongStrip();
      break;
  }
  FastLED.show();   // nothing appears on the strip until show() sends the leds[] array
}

// Moves the current fade one step and gives FastLED the brightness to use.
// progress runs from 0 (start of the fade) to 255 (finished);
// lerp8by8(a, b, progress) is FastLED's "the value progress/255 of the way
// from a to b", and blend() does the same for colours.
void updateFade(uint32_t now) {
  uint32_t elapsed = now - fadeStartAt;
  if (fadeLength == 0 || elapsed >= fadeLength) {
    outputLevel = fadeTo;          // fade finished (or there wasn't one)
    colourShown = solidColour;
  } else {
    uint8_t progress = (uint64_t)elapsed * 255 / fadeLength;   // uint64_t: room for long fades
    outputLevel = lerp8by8(fadeFrom, fadeTo, progress);
    colourShown = blend(colourFrom, solidColour, progress);
  }
  FastLED.setBrightness(outputLevel);
}

// Scales a per-frame step by the effect speed: the same amount at speed 5,
// a quarter of it at speed 1, four times it at speed 10. Never 0, so an
// effect never freezes.
uint16_t bySpeed(uint16_t amount) {
  uint32_t scaled = (uint32_t)amount * SPEED_STEPS[lamp.speed] / 256;
  return scaled > 0 ? scaled : 1;
}

// Spreads the active palette along the strip, shifted by the animation phase.
// Because animationPhase grows every frame (by 1 at speed 5), the colours appear to move.
void drawPaletteAlongStrip() {
  const uint8_t spacing = 4;   // palette steps between neighbouring LEDs
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    // ColorFromPalette(palette, position 0-255, brightness, blending).
    // Positions above 255 wrap round, because they're stored in a uint8_t.
    leds[i] = ColorFromPalette(activePalette, animationPhase + i * spacing, 255, activeBlend);
  }
}

// Candle mode: a warm flame at the bottom of the lamp, brightest at its base
// and dimmer and redder towards the tip.
//   * slow noise makes the whole flame "breathe"
//   * faster noise makes each LED shimmer on its own
//   * now and then a "draught" makes the flame dip and recover
// inoise8() is FastLED's Perlin noise: it returns 0-255 values that change
// smoothly rather than jumping about like random numbers, which is why the
// flicker looks natural.
void drawCandle() {
  static uint16_t noiseClock = 0;
  static uint8_t draught = 0;          // how far the flame is currently dipping

  noiseClock += bySpeed(7);            // += adds to the variable: noiseClock = noiseClock + 7 (at speed 5)
  if (draught > 0) {
    draught = qsub8(draught, 5);       // recover over a few hundred milliseconds
  } else if (random8() == 0) {         // random8() is 0-255, so 1 frame in 256: about every 5 s
    draught = random8(60, 130);
  }

  // qadd8 / qsub8 / scale8 are FastLED's safe 8-bit maths: qadd8 stops at 255
  // instead of wrapping round to 0, qsub8 stops at 0, and scale8(a, b) means
  // "a times b/256" - a quick way to take a fraction of a value.
  uint8_t flame = qadd8(150, scale8(inoise8(noiseClock), 105));   // overall level 150-255
  flame = qsub8(flame, draught);

  const CRGB base(255, 147, 41);       // the yellow-orange of a candle flame
  const CRGB tip(255, 64, 6);          // deeper orange at the top of the flame

  FastLED.clear();                     // LEDs above the flame stay dark
  // && means "and": stop at CANDLE_LEDS, but never go past the end of the strip.
  for (uint8_t i = 0; i < CANDLE_LEDS && i < LED_COUNT; i++) {
    // height: 0 at the base of the flame, 255 at its tip. The (uint16_t)
    // "cast" converts i to a bigger type first, so i * 255 can't overflow.
    uint8_t height = (CANDLE_LEDS > 1) ? (uint16_t)i * 255 / (CANDLE_LEDS - 1) : 0;
    uint8_t shimmer = inoise8((uint16_t)i * 50, noiseClock * 3);

    uint8_t level = scale8(flame, 255 - scale8(height, 180));   // fade out towards the tip
    level = scale8(level, 190 + scale8(shimmer, 65));           // per-LED shimmer

    CRGB colour = blend(base, tip, height);   // mix base and tip colours by height
    colour.nscale8_video(level);              // dim to the level, never fully off

    // Count from the start or the end of the strip, depending on how it is mounted.
    uint8_t index = CANDLE_AT_LED_ZERO ? i : (LED_COUNT - 1 - i);
    leds[index] = colour;
  }
}

// --- Effects added in v4.1 ----------------------------------------------------------

// Fireplace: glowing embers along the whole bar. Each LED follows its own slow
// noise (so neighbours glow together, like a bed of coals), with a little
// random crackle on top, coloured through FastLED's "heat" palette
// (dark red -> orange -> yellow). The level is kept between 30 and 180 so it
// never goes fully black or white-hot.
void drawFireplace() {
  static uint16_t noiseClock = 0;
  noiseClock += bySpeed(9);
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    uint8_t heat = inoise8(i * 60, noiseClock);           // smooth 0-255 per LED
    heat = qadd8(scale8(heat, 220), random8(30));          // plus a little crackle
    leds[i] = ColorFromPalette(HeatColors_p, qadd8(30, scale8(heat, 150)), 255, LINEARBLEND);
  }
}

// Breathe: the chosen colour slowly brightens and dims, like breathing.
// cubicwave8() turns the steadily rising animationPhase into a smooth
// up-and-down wave (0 -> 255 -> 0); at speed 5 one breath takes about 5 s.
void drawBreathe() {
  uint8_t wave = cubicwave8(animationPhase);
  uint8_t level = 30 + scale8(wave, 225);   // never quite dark: 30-255
  CRGB colour = colourShown;
  colour.nscale8_video(level);              // nscale8_video dims without ever reaching 0
  fill_solid(leds, LED_COUNT, colour);
}

// Twinkle: a dim background in the chosen colour, with "stars" that light up
// and slowly fade back. Unlike the other effects it builds on the previous
// frame: every LED is nudged towards the background a little each frame.
void drawTwinkle() {
  CRGB background = colourShown;
  background.nscale8_video(26);             // about 10% of the colour
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    nblend(leds[i], background, 8);         // move 8/255 of the way towards the background
  }
  // A new star now and then; more often at higher speeds.
  if (random8() < 6 + lamp.speed * 4) {
    uint16_t i = random16(LED_COUNT);
    leds[i] = blend(colourShown, CRGB::White, 140);   // a pale, sparkly version of the colour
  }
}

// Comet: a bright dot in the chosen colour that travels end to end and back,
// leaving a fading tail. fadeToBlackBy() dims every LED a little each frame,
// so the places the comet has been fade out behind it.
void drawComet() {
  fadeToBlackBy(leds, LED_COUNT, 40);
  // triwave8() turns the rising phase into 0 -> 255 -> 0 (there and back);
  // scale that to an LED position 0 .. LED_COUNT-1.
  uint16_t position = (uint16_t)triwave8(animationPhase) * (LED_COUNT - 1) / 255;
  leds[position] = colourShown;
}

// Reading light: a white of READING_LIGHT_KELVIN, full brightness in the
// middle of the bar and dimmer towards the ends.
void drawReadingLight() {
  CRGB white = kelvinToRgb(READING_LIGHT_KELVIN);
  white = CRGB(applyGamma_video(white.r, COLOUR_GAMMA), applyGamma_video(white.g, COLOUR_GAMMA),
               applyGamma_video(white.b, COLOUR_GAMMA));
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    // Distance from the middle: 0 in the centre, 255 at either end.
    // abs() gives the size of a number without its sign: abs(-5) is 5.
    int32_t fromCentre = abs((int32_t)(2 * i) - (int32_t)(LED_COUNT - 1));
    uint8_t distance = (LED_COUNT > 1) ? fromCentre * 255 / (LED_COUNT - 1) : 0;
    // ease8InOutQuad keeps the middle bright for longer before it falls off.
    uint8_t level = 255 - scale8(ease8InOutQuad(distance), 210);   // 255 in the middle, 45 at the ends
    leds[i] = white;
    leds[i].nscale8_video(level);
  }
}

// Sunrise: the whole bar goes from dark through deep red and orange to warm
// white over sunriseLength, then stays at warm white. The colours come from a
// "gradient palette": a list of (position, red, green, blue) stops that
// FastLED blends between. Position 0 is the start of the sunrise, 255 the end.
DEFINE_GRADIENT_PALETTE(SUNRISE_GRADIENT) {
    0,   0,   0,   0,    // dark
   50,  40,   2,   0,    // first glow: very dim red
  110, 140,  20,   0,    // deep red
  170, 255,  80,   5,    // orange
  220, 255, 150,  50,    // golden
  255, 255, 190, 110,    // warm white
};

void drawSunrise(uint32_t now) {
  static const CRGBPalette16 sunrise = SUNRISE_GRADIENT;
  uint32_t elapsed = now - sunriseStartAt;
  uint8_t progress = (sunriseLength == 0 || elapsed >= sunriseLength)
                     ? 255 : (uint64_t)elapsed * 255 / sunriseLength;
  // scale8(progress, 240): a 16-colour palette's last colour sits at position
  // 240. Positions above that blend back towards the *first* colour (black),
  // so the sunrise would end dark - stopping at 240 keeps it at warm white.
  fill_solid(leds, LED_COUNT, ColorFromPalette(sunrise, scale8(progress, 240), 255, LINEARBLEND));
}

// Random mode: usually one of FastLED's built-in palettes, sometimes a freshly
// generated four-colour gradient; one change in four uses hard steps.
void pickRandomPalette() {
  static const TProgmemRGBPalette16 *const choices[] = {
    &RainbowColors_p, &RainbowStripeColors_p, &CloudColors_p, &PartyColors_p,
    &OceanColors_p,   &ForestColors_p,        &LavaColors_p,  &HeatColors_p,
  };
  const uint8_t choiceCount = sizeof(choices) / sizeof(choices[0]);

  if (random8(4) == 0) {   // random8(4) is 0, 1, 2 or 3: a 1-in-4 chance
    // CHSV describes a colour by hue (0-255 around the colour wheel),
    // saturation and value (brightness). Spacing the hues out makes a gradient.
    uint8_t hue = random8();
    activePalette = CRGBPalette16(CHSV(hue, 255, 255),
                                  CHSV(hue + 64 + random8(64), 255, 255),
                                  CHSV(hue + 128, 200, 255),
                                  CHSV(hue + 192 + random8(64), 255, 255));
  } else {
    activePalette = *choices[random8(choiceCount)];
  }
  activeBlend = (random8(4) == 0) ? NOBLEND : LINEARBLEND;
}


// ===========================================================================
// 7. Saving settings in flash
// ===========================================================================
// The ESP8266 has no real EEPROM chip; the EEPROM library uses a small piece
// of flash memory instead. Flash wears out after roughly 10,000-100,000
// writes, so the sketch saves only when something changed, and waits until
// SAVE_DELAY_MS after the last change (dragging the brightness slider would
// otherwise write dozens of times).

// Change this number whenever StoredSettings changes, so old saved data in
// the wrong format is ignored instead of being misread.
// v4.1 added `speed` and `kelvin` at the end and moved from 0xC1 to 0xC2.
// Because the first seven bytes didn't change, settings saved by an older
// version (0xC1) can still be read - see loadSettings().
const uint8_t SETTINGS_MARKER     = 0xC2;   // 0x means hexadecimal: 0xC2 = 194
const uint8_t SETTINGS_MARKER_OLD = 0xC1;   // what v3.x and v4.0 wrote

struct StoredSettings {
  uint8_t marker;
  uint8_t mode, palette, brightness;
  uint8_t red, green, blue;
  uint8_t speed;      // new in v4.1
  uint16_t kelvin;    // new in v4.1
};

void loadSettings() {
  EEPROM.begin(sizeof(StoredSettings));   // reserve exactly as many bytes as we store
  StoredSettings s = {};                  // = {} starts every field at 0
  EEPROM.get(0, s);                       // copy the saved bytes into s
  if (s.marker == SETTINGS_MARKER || s.marker == SETTINGS_MARKER_OLD) {
    // Only accept values that make sense; a garbled byte keeps the default.
    if (s.mode >= MODE_MIN && s.mode <= MODE_MAX) lamp.mode = s.mode;
    if (s.palette < PALETTE_COUNT) lamp.palette = s.palette;
    if (s.brightness > 0) lamp.brightness = s.brightness;
    lamp.colour = CRGB(s.red, s.green, s.blue);
    if (s.marker == SETTINGS_MARKER) {    // only the new format has these
      if (s.speed >= SPEED_MIN && s.speed <= SPEED_MAX) lamp.speed = s.speed;
      if (s.kelvin >= MIN_KELVIN && s.kelvin <= MAX_KELVIN) lamp.kelvin = s.kelvin;
    }
    Serial.println("Settings restored");
  } else {
    Serial.println("No saved settings - using defaults");
  }
  // Never restart a sunrise after a power cut (it might be the middle of the
  // night): come back in Solid colour instead.
  if (lamp.mode == MODE_SUNRISE) lamp.mode = MODE_SOLID;
  lamp.on = true;
  lampChanged();         // fades up from dark, so power-up is gentle too
  savePending = false;   // nothing new to save yet
}

void saveSettingsIfDue() {
  if (!savePending || millis() - lastChangeAt < SAVE_DELAY_MS) return;
  savePending = false;

  StoredSettings wanted = { SETTINGS_MARKER, lamp.mode, lamp.palette, lamp.brightness,
                            lamp.colour.r, lamp.colour.g, lamp.colour.b, lamp.speed, lamp.kelvin };
  StoredSettings current = {};
  EEPROM.get(0, current);
  // memcmp compares two blocks of memory byte by byte; 0 means identical.
  if (memcmp(&wanted, &current, sizeof(StoredSettings)) == 0) return;   // unchanged: skip the write

  EEPROM.put(0, wanted);
  EEPROM.commit();   // actually writes to flash
  Serial.println("Settings saved");
}


// ===========================================================================
// 8. Wi-Fi, OTA and Home Assistant
// ===========================================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);          // STA = "station": join a network (not create one)
  WiFi.hostname(DEVICE_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Joining Wi-Fi \"%s\"", WIFI_SSID);   // \" puts a quote mark inside text

  // This is the one place the sketch waits, because nothing else can work
  // without Wi-Fi. It still draws frames while waiting.
  uint32_t lastDot = millis();
  while (WiFi.status() != WL_CONNECTED) {   // while = repeat as long as the condition is true
    drawFrame();
    delay(5);                   // short pause; also lets the Wi-Fi system do its work
    if (millis() - lastDot >= 500) {
      lastDot = millis();
      Serial.print('.');
    }
  }
  Serial.printf("\nWi-Fi connected - http://%s/  (%s.local)\n",
                WiFi.localIP().toString().c_str(), DEVICE_NAME);
}

// OTA = "over the air": PlatformIO can send new firmware over Wi-Fi.
void startOta() {
  ArduinoOTA.setHostname(DEVICE_NAME);
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);   // only if secrets.h defines one
#endif
  // The []() { ... } blocks are *lambdas* (C++): small unnamed functions
  // written right where they're needed. ArduinoOTA calls them at each stage.
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update starting");
    FastLED.clear(true);   // LEDs off while flashing
    // Tell Home Assistant straight away, rather than after the connection times out.
    if (mqtt.connected()) mqtt.publish(topicAvailability.c_str(), "offline", true);
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA update finished - restarting"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA update failed, error %u\n", error); });
  ArduinoOTA.begin();
}

// ---------------------------------------------------------------------------
// Home Assistant over MQTT
// ---------------------------------------------------------------------------
// MQTT is a messaging system. Devices connect to one central program, the
// *broker* (here Mosquitto, on the Pi5), and either *publish* messages to a
// named *topic* ("bedlight/state") or *subscribe* to topics to receive them.
// The lamp and Home Assistant never talk directly - both talk to the broker.
//
//   Topic                          Direction           Contents
//   bedlight/set                   HA -> lamp          {"state":"ON","brightness":128,...}
//   bedlight/palette/set           HA -> lamp          Ocean
//   bedlight/speed/set             HA -> lamp          5        (effect speed 1-10)
//   bedlight/sleep/set             HA -> lamp          30       (sleep timer, minutes; 0 = off)
//   bedlight/state                 lamp -> HA          {"state":"ON","effect":"Candle",...}
//   bedlight/availability          lamp -> HA          online / offline
//   homeassistant/status           HA -> lamp          online (when HA starts)
//   homeassistant/<type>/.../config  lamp -> HA        "discovery": what the lamp is
//
// Discovery is what makes the lamp appear in Home Assistant by itself. The
// lamp publishes a description of each thing it offers (a light, a palette
// selector, speed and sleep-timer sliders and two diagnostic sensors), and
// Home Assistant creates them.
//
// Messages can be *retained*: the broker keeps the last one on each topic
// and hands it to anyone who subscribes later. Discovery, state and
// availability are retained, so Home Assistant knows about the lamp even if
// it starts after the lamp does.

// Keeps the MQTT connection alive. When it is down, reconnect attempts start
// every MQTT_RETRY_MS and back off to once a minute if they keep failing.
// The lamp, the web page and Alexa keep working in between.
void serviceMqtt() {
  if (mqtt.connected()) {
    mqtt.loop();   // receive messages (which calls onMqttMessage) and keep the connection alive
    return;
  }

  // `static` variables inside a function keep their value between calls
  // (a normal local variable would start again from scratch every time).
  static uint32_t lastAttemptAt = 0;
  static uint32_t retryDelay = 0;          // 0 = try straight away
  if (millis() - lastAttemptAt < retryDelay) return;
  if (WiFi.status() != WL_CONNECTED) return;
  lastAttemptAt = millis();

  Serial.printf("MQTT: connecting to %s:%u as %s... ", MQTT_HOST, MQTT_PORT, MQTT_USER);
  // The last three values are the "last will": if the lamp disappears without
  // saying goodbye (power cut, crash), the broker publishes "offline" to the
  // availability topic on its behalf, and Home Assistant greys the lamp out.
  //   connect(client ID, user, password, will topic, will QoS, will retained, will message)
  if (mqtt.connect(deviceId, MQTT_USER, MQTT_PASSWORD,
                   topicAvailability.c_str(), 0, true, "offline")) {
    Serial.println("connected");
    // .c_str() turns a String into plain C text, which the library expects.
    mqtt.subscribe(topicSet.c_str());
    mqtt.subscribe(topicPaletteSet.c_str());
    mqtt.subscribe(topicSpeedSet.c_str());
    mqtt.subscribe(topicSleepSet.c_str());
    mqtt.subscribe(HA_STATUS_TOPIC);
    mqtt.publish(topicAvailability.c_str(), "online", true);   // true = retained
    discoveryPending = true;   // (re)announce the lamp...
    statePending = true;       // ...and report its state, from loop()
    retryDelay = MQTT_RETRY_MS;
    return;
  }

  // mqtt.state() says why: -2 = broker not reachable (wrong address, or a
  // firewall in the way), 4 = wrong user or password, 5 = not authorised.
  Serial.printf("failed (state %d)\n", mqtt.state());   // %d = whole number that may be negative
  // Back off: wait longer after each failure, up to a minute.
  if (retryDelay == 0)          retryDelay = MQTT_RETRY_MS;
  else if (retryDelay < 30000)  retryDelay *= 2;   // *= doubles it: retryDelay = retryDelay * 2
  else                          retryDelay = 60000;
  Serial.printf("  retrying in %lu s\n", (unsigned long)(retryDelay / 1000));
}

// Called by the MQTT library whenever a message arrives on a topic we
// subscribed to. The message arrives as raw bytes (payload) with a length,
// not as ready-made text.
void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  // Print it. %.*s prints exactly `length` characters - needed because the
  // payload has no end-of-text marker. (int) converts the length to the type
  // printf expects for the *.
  Serial.printf("MQTT: %s = %.*s\n", topic, (int)length, (const char *)payload);

  if (topicSet == topic) {
    handleLightCommand(payload, length);
    return;
  }

  if (topicPaletteSet == topic) {
    // The palette selector sends the palette's name, e.g. "Ocean".
    // strncmp compares at most `length` characters; 0 means they match.
    for (uint8_t i = 0; i < PALETTE_COUNT; i++) {
      if (strlen(PALETTES[i].name) == length &&
          strncmp(PALETTES[i].name, (const char *)payload, length) == 0) {
        setPalette(i);
        return;
      }
    }
    Serial.println("  unknown palette");
    return;
  }

  if (topicSpeedSet == topic) {
    setSpeed(constrain(payloadToNumber(payload, length), (long)SPEED_MIN, (long)SPEED_MAX));
    return;
  }

  if (topicSleepSet == topic) {
    setSleepTimer(constrain(payloadToNumber(payload, length), 0L, (long)SLEEP_MAX_MINUTES));
    return;
  }

  // Home Assistant has (re)started: it forgets nothing retained, but sending
  // the discovery and state again is what Home Assistant recommends.
  if (strcmp(topic, HA_STATUS_TOPIC) == 0 && length == 6 &&
      strncmp((const char *)payload, "online", 6) == 0) {
    discoveryPending = true;
    statePending = true;
  }
}

// A light command from Home Assistant, in JSON. Examples:
//   {"state":"OFF"}
//   {"state":"ON"}
//   {"state":"ON","brightness":77}
//   {"state":"ON","color":{"r":255,"g":0,"b":128}}
//   {"state":"ON","color_temp":2700}                 (white, in kelvin)
//   {"state":"ON","effect":"Candle"}
//   {"state":"OFF","transition":600}                 (fade out over 10 minutes)
//   {"state":"ON","effect":"Sunrise","transition":1800}   (a 30-minute sunrise)
// One command can carry several of these at once, so check each of them.
void handleLightCommand(byte *payload, unsigned int length) {
  // A JsonDocument holds JSON that has been taken apart into its pieces.
  // deserializeJson() does the taking apart; it returns an error if the
  // text isn't valid JSON.
  JsonDocument command;
  DeserializationError error = deserializeJson(command, payload, length);
  if (error) {
    Serial.printf("  not valid JSON (%s)\n", error.c_str());
    return;
  }

  // "transition" is in seconds (it may have decimals, e.g. 0.5). If it's
  // there, every change in this command fades over that long instead of
  // DEFAULT_FADE_MS. requestedFadeMs is put back afterwards, so the next
  // change from the web page or Alexa uses the normal fade again.
  if (command["transition"].is<float>()) {
    float seconds = constrain(command["transition"].as<float>(), 0.0f, (float)MAX_TRANSITION_S);
    requestedFadeMs = seconds * 1000;
    transitionGiven = true;
  }
  applyLightCommand(command);
  requestedFadeMs = DEFAULT_FADE_MS;
  transitionGiven = false;
}

// Carries out the parts of a light command (see handleLightCommand).
void applyLightCommand(JsonDocument &command) {
  // command["state"] reads one field. `| ""` means "or empty text if the
  // field is missing", so `state` is never a null pointer.
  const char *state = command["state"] | "";
  if (strcmp(state, "OFF") == 0) {   // strcmp() returns 0 when two texts are equal
    setPower(false);
    return;
  }

  // A white temperature in kelvin (we told Home Assistant to use kelvin).
  if (command["color_temp"].is<int>()) {
    setColourTemperature(command["color_temp"].as<int>());   // also selects Solid colour
  }

  // is<JsonObject>() checks that "color" exists and is a { ... } group.
  if (command["color"].is<JsonObject>()) {
    JsonObject colour = command["color"];
    setColour(CRGB(colour["r"] | 0, colour["g"] | 0, colour["b"] | 0));   // also selects Solid colour
  }

  // An effect is one of our modes, by name (see MODE_NAMES).
  if (command["effect"].is<const char *>()) {
    const char *effect = command["effect"];
    bool found = false;
    for (uint8_t mode = MODE_MIN; mode <= MODE_MAX; mode++) {
      if (strcmp(effect, MODE_NAMES[mode]) == 0) {
        setMode(mode);
        found = true;
        break;   // stop the for loop: we found it
      }
    }
    if (!found) Serial.println("  unknown effect");
  }

  // Home Assistant's brightness is 0-255, the same scale as ours.
  if (command["brightness"].is<int>()) {
    setBrightness(constrain(command["brightness"].as<int>(), 1, 255));
  }

  // Any command other than OFF means the lamp should be on. (setColour and
  // setMode switch it on already; this covers a plain {"state":"ON"}.)
  if (!lamp.on) setPower(true);
}

// Turns a message like "5" or "30.0" into a whole number. Home Assistant's
// number sliders send plain text, which may include ".0".
long payloadToNumber(byte *payload, unsigned int length) {
  char text[16];                                    // plenty for any number
  unsigned int n = (length < sizeof(text) - 1) ? length : sizeof(text) - 1;
  memcpy(text, payload, n);                         // copy the bytes...
  text[n] = '\0';                                   // ...and add the end-of-text marker
  return lround(atof(text));                        // atof reads decimals; lround rounds to whole
}

// Sends a JsonDocument to a topic as one retained message.
// Instead of building the whole text in memory first, it tells the broker
// how long the message will be (measureJson), then serializeJson() writes it
// straight into the connection. That way even long messages need no extra
// memory, and they aren't limited by the 512-byte receive buffer.
bool publishJson(const String &topic, JsonDocument &doc) {
  size_t length = measureJson(doc);
  if (!mqtt.beginPublish(topic.c_str(), length, true)) return false;   // true = retained
  serializeJson(doc, mqtt);
  return mqtt.endPublish();
}

// The "device" part of every discovery message. Home Assistant groups all
// entities with the same identifiers into one device card, and shows the
// rest (maker, model, firmware, a link to the web page) on that card.
void addDeviceInfo(JsonDocument &doc) {
  // doc["device"].to<JsonObject>() creates an empty { } group called "device"
  // and gives us a handle to fill it in.
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"].to<JsonArray>().add(deviceId);   // a [ ] list with one entry
  device["name"]         = LAMP_NAME;
  device["manufacturer"] = "DIY (ESP8266 RGB Controller)";
  device["model"]        = "Wemos D1 Mini + " + String(LED_COUNT) + " WS2812B LEDs";
  device["sw_version"]   = FIRMWARE_VERSION;
  device["configuration_url"] = "http://" + WiFi.localIP().toString() + "/";

  // "origin" tells Home Assistant which software sent the discovery message.
  JsonObject origin = doc["origin"].to<JsonObject>();
  origin["name"]        = "ESP8266 RGB Controller";
  origin["sw_version"]  = FIRMWARE_VERSION;
  origin["support_url"] = "https://github.com/gcinini/ESP8266-RGB-Controller";
}

// Announces the lamp to Home Assistant: one retained "config" message per
// entity, on <prefix>/<entity type>/<deviceId>/<what>/config.
void publishDiscovery() {
  String base = String(HA_DISCOVERY_PREFIX) + "/";
  String node = String("/") + deviceId + "/";
  String id = deviceId;
  bool ok = true;

  // 1. The light: on/off, brightness, colour, white temperature, and the
  //    modes as "effects". Fades ("transition") are supported by default.
  {   // the braces make `light` exist only inside this block, freeing its memory at the end
    JsonDocument light;
    light["name"] = nullptr;   // null = use the device name ("Bedlight") for the light itself
    light["unique_id"]          = id + "_light";
    light["schema"]             = "json";   // commands and state are JSON, as in handleLightCommand()
    light["command_topic"]      = topicSet;
    light["state_topic"]        = topicState;
    light["availability_topic"] = topicAvailability;
    light["brightness"]         = true;
    JsonArray colourModes = light["supported_color_modes"].to<JsonArray>();
    colourModes.add("color_temp");          // whites, picked by temperature
    colourModes.add("rgb");                 // any colour
    light["color_temp_kelvin"]  = true;     // send colour temperatures in kelvin, not mireds
    light["min_kelvin"]         = MIN_KELVIN;
    light["max_kelvin"]         = MAX_KELVIN;
    light["effect"]             = true;
    JsonArray effects = light["effect_list"].to<JsonArray>();
    for (uint8_t mode = MODE_MIN; mode <= MODE_MAX; mode++) effects.add(MODE_NAMES[mode]);
    addDeviceInfo(light);
    ok &= publishJson(base + "light" + node + "light/config", light);   // a &= b: ok stays true only if b is true
  }

  // 2. A drop-down list for the palette.
  {
    JsonDocument select;
    select["name"]               = "Palette";
    select["unique_id"]          = id + "_palette";
    select["icon"]               = "mdi:palette";
    select["command_topic"]      = topicPaletteSet;
    select["state_topic"]        = topicState;
    select["value_template"]     = "{{ value_json.palette }}";   // read "palette" from the state JSON
    select["availability_topic"] = topicAvailability;
    JsonArray options = select["options"].to<JsonArray>();
    for (uint8_t i = 0; i < PALETTE_COUNT; i++) options.add(PALETTES[i].name);
    addDeviceInfo(select);
    ok &= publishJson(base + "select" + node + "palette/config", select);
  }

  // 3. Wi-Fi signal strength, shown under "Diagnostic" on the device card.
  {
    JsonDocument sensor;
    sensor["name"]                = "Wi-Fi signal";
    sensor["unique_id"]           = id + "_rssi";
    sensor["device_class"]        = "signal_strength";
    sensor["unit_of_measurement"] = "dBm";
    sensor["state_class"]         = "measurement";
    sensor["entity_category"]     = "diagnostic";
    sensor["state_topic"]         = topicState;
    sensor["value_template"]      = "{{ value_json.rssi }}";
    sensor["availability_topic"]  = topicAvailability;
    addDeviceInfo(sensor);
    ok &= publishJson(base + "sensor" + node + "rssi/config", sensor);
  }

  // 4. Effect speed, as a slider from 1 to 10.
  {
    JsonDocument number;
    number["name"]               = "Effect speed";
    number["unique_id"]          = id + "_speed";
    number["icon"]               = "mdi:speedometer";
    number["command_topic"]      = topicSpeedSet;
    number["state_topic"]        = topicState;
    number["value_template"]     = "{{ value_json.speed }}";
    number["min"]                = SPEED_MIN;
    number["max"]                = SPEED_MAX;
    number["step"]               = 1;
    number["mode"]               = "slider";
    number["availability_topic"] = topicAvailability;
    addDeviceInfo(number);
    ok &= publishJson(base + "number" + node + "speed/config", number);
  }

  // 5. Sleep timer, as a slider in minutes (0 = off). While a timer runs,
  //    the state reports the minutes left, so the slider counts down.
  {
    JsonDocument number;
    number["name"]                = "Sleep timer";
    number["unique_id"]           = id + "_sleep";
    number["icon"]                = "mdi:timer-sand";
    number["command_topic"]       = topicSleepSet;
    number["state_topic"]         = topicState;
    number["value_template"]      = "{{ value_json.sleep }}";
    number["min"]                 = 0;
    number["max"]                 = SLEEP_MAX_MINUTES;
    number["step"]                = 5;
    number["unit_of_measurement"] = "min";
    number["mode"]                = "slider";
    number["availability_topic"]  = topicAvailability;
    addDeviceInfo(number);
    ok &= publishJson(base + "number" + node + "sleep/config", number);
  }

  // 6. The lamp's IP address, also a diagnostic.
  {
    JsonDocument sensor;
    sensor["name"]               = "IP address";
    sensor["unique_id"]          = id + "_ip";
    sensor["icon"]               = "mdi:ip-network";
    sensor["entity_category"]    = "diagnostic";
    sensor["state_topic"]        = topicState;
    sensor["value_template"]     = "{{ value_json.ip }}";
    sensor["availability_topic"] = topicAvailability;
    addDeviceInfo(sensor);
    ok &= publishJson(base + "sensor" + node + "ip/config", sensor);
  }

  Serial.println(ok ? "MQTT: announced to Home Assistant" : "MQTT: announcing to Home Assistant failed");
  if (ok) discoveryPending = false;   // otherwise loop() tries again
}

// Reports what the lamp is doing, for example:
//   {"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":255,"g":180,"b":107},
//    "effect":"Candle","palette":"Ocean","speed":5,"sleep":0,"rssi":-63,"ip":"192.168.30.30"}
// or, for a white picked by temperature, "color_mode":"color_temp","color_temp":2700.
// The light reads state/brightness/colour/effect; the palette selector, the
// sliders and the sensors pick out their own field with their value_template.
void publishState() {
  JsonDocument state;
  state["state"]      = lamp.on ? "ON" : "OFF";
  state["brightness"] = lamp.brightness;
  if (lamp.kelvin != 0) {
    state["color_mode"] = "color_temp";
    state["color_temp"] = lamp.kelvin;
  } else {
    state["color_mode"] = "rgb";
    JsonObject colour = state["color"].to<JsonObject>();
    colour["r"] = lamp.colour.r;
    colour["g"] = lamp.colour.g;
    colour["b"] = lamp.colour.b;
  }
  state["effect"]  = MODE_NAMES[lamp.mode];
  state["palette"] = PALETTES[lamp.palette].name;
  state["speed"]   = lamp.speed;
  state["sleep"]   = sleepMinutesLeft();
  state["rssi"]    = WiFi.RSSI();
  state["ip"]      = WiFi.localIP().toString();
  if (publishJson(topicState, state)) {
    statePending = false;
    lastStateAt = millis();
    if (lastStateAt == 0) lastStateAt = 1;   // never store 0, which means "never sent"
  }
}

// Called from loop(). Sends the discovery when it's needed, and the state
// STATE_SETTLE_MS after a change (so a burst of changes - like dragging the
// brightness slider - becomes one report) or every STATE_HEARTBEAT_MS.
void publishToHomeAssistantIfDue() {
  if (!mqtt.connected()) return;
  if (discoveryPending) publishDiscovery();

  uint32_t now = millis();
  bool heartbeatDue = (lastStateAt == 0) || (now - lastStateAt >= STATE_HEARTBEAT_MS);
  if (!statePending && !heartbeatDue) return;
  if (now - lastChangeAt < STATE_SETTLE_MS) return;
  publishState();
}

// How long the lamp has been running, e.g. "2h 05m" or "3d 4h 10m".
String uptimeText() {
  uint32_t minutes = millis() / 60000;   // whole-number division drops the remainder
  uint32_t days = minutes / 1440, hours = (minutes / 60) % 24, mins = minutes % 60;
  char text[24];
  // snprintf writes formatted text into a buffer, never more than sizeof(text).
  if (days > 0) snprintf(text, sizeof(text), "%lud %luh %lum", (unsigned long)days, (unsigned long)hours, (unsigned long)mins);
  else          snprintf(text, sizeof(text), "%luh %02lum", (unsigned long)hours, (unsigned long)mins);
  return String(text);
}


// ===========================================================================
// 9. Alexa
// ===========================================================================
// How it works:
//   1. "Alexa, discover devices": the Echo sends a search message (SSDP) to
//      every device on the local network. Espalexa answers "I am a Hue bridge".
//   2. The Echo asks for the bridge's description (/description.xml) and its
//      list of lights (/api/.../lights), and finds one colour light,
//      LAMP_NAME. These requests arrive on our own web server (port 80).
//   3. "Alexa, set the lightbox to blue": the Echo sends a small JSON message
//      to /api/.../lights/1/state, Espalexa decodes it and calls
//      onAlexaCommand() below.
// The Echo and the lamp must be on the same network (VLAN30 / gc-dev here),
// because the search message doesn't cross into other networks.

void startAlexa() {
#if ENABLE_ALEXA
  // `new` creates an object that lives until the program ends (a function's
  // normal variables disappear when it returns). EspalexaDeviceType::color
  // tells Alexa this is a bulb that supports colours, not just on/off.
  alexaLight = new EspalexaDevice(LAMP_NAME, onAlexaCommand, EspalexaDeviceType::color, lamp.brightness);
  espalexa.addDevice(alexaLight);
  syncAlexa();                 // start with the lamp's real state
  // Share our web server with Espalexa. begin() adds its pages, starts
  // listening for Echo searches, and then starts the web server. It returns
  // false if it could not listen for searches - and in that case it does NOT
  // start the web server, so we start it ourselves to keep the web page working.
  if (espalexa.begin(&web)) {
    Serial.printf("Alexa: ready as \"%s\" - say \"Alexa, discover devices\"\n", LAMP_NAME);
  } else {
    web.begin();
    Serial.println("Alexa: could not start (multicast failed) - web page still works");
  }
#else
  web.begin();                 // without Alexa, start the web server ourselves
#endif
}

#if ENABLE_ALEXA
// Called by Espalexa whenever Alexa sends a command. `device` points to the
// bulb, which now holds the values Alexa asked for. One message can carry
// several changes at once (for example "on", a brightness and a colour), so
// look at each value instead of only the last thing that changed.
void onAlexaCommand(EspalexaDevice *device) {
  // `->` reads a field or calls a method through a pointer: device->getValue()
  // means "getValue() of the object device points to".
  EspalexaDeviceProperty changed = device->getLastChangedProperty();
  Serial.printf("Alexa: command (property %u, brightness %u)\n", (unsigned)changed, device->getValue());

  if (changed == EspalexaDeviceProperty::off) {
    setPower(false);
    return;
  }

  // A colour request: hs = hue and saturation ("blue"), xy = a colour chart
  // position; Espalexa turns both into ordinary red, green and blue for us.
  // ct = colour temperature ("warm white"), which Alexa sends in "mireds":
  // 1,000,000 divided by the temperature in kelvin, so we turn it back.
  if (changed == EspalexaDeviceProperty::ct && device->getCt() > 0) {
    setColourTemperature(1000000UL / device->getCt());   // also selects Solid colour
  } else if (changed == EspalexaDeviceProperty::hs || changed == EspalexaDeviceProperty::xy) {
    setColour(CRGB(device->getR(), device->getG(), device->getB()));   // also selects Solid colour
  }

  // Brightness 1-255 ("set the lightbox to 40%"). getValue() is 0 only when off.
  uint8_t level = device->getValue();
  if (level > 0 && level != lamp.brightness) setBrightness(level);

  // Any command other than "off" means the lamp should be on. Turning on keeps
  // the current mode, so "Alexa, turn on the lightbox" brings back Candle or
  // Rainbow if that was showing.
  if (!lamp.on) setPower(true);
}
#endif

// Copies the lamp's state into the Alexa bulb, so the Alexa app shows the
// right on/off, brightness and colour after changes from the web page or
// Home Assistant. Called from lampChanged(); does nothing without Alexa.
void syncAlexa() {
#if ENABLE_ALEXA
  if (alexaLight == nullptr) return;   // not created yet (during start-up)
  alexaLight->setValue(lamp.on ? lamp.brightness : 0);   // 0 means "off" to Alexa
  // Skip black (0,0,0): Espalexa's colour conversion divides by a total that
  // is zero for black, which would send Alexa an invalid answer.
  if (lamp.colour.r || lamp.colour.g || lamp.colour.b) {
    alexaLight->setColor(lamp.colour.r, lamp.colour.g, lamp.colour.b);
  }
#endif
}


// ===========================================================================
// 10. Web interface
// ===========================================================================
// The web page (web_page.h) is one HTML file with JavaScript in it. It talks
// to the lamp through these addresses; every one answers with the lamp's
// current state as JSON (a text format JavaScript can read directly).
//   /state
//   /power?on=1|0
//   /colour?hex=RRGGBB        (a colour name also works)
//   /brightness?value=1-255
//   /mode?id=1-14
//   /palette?id=0-5
//   /temperature?kelvin=2000-6500   (a white by colour temperature)
//   /speed?value=1-10               (effect speed)
//   /sleep?minutes=0-120            (sleep timer; 0 cancels it)
//
// web.on("/address", function) registers a handler: "when a browser asks for
// this address, run this function". The functions here are lambdas (see
// startOta). web.arg("name") reads a value from the address, e.g. the 3 in
// /mode?id=3.
void startWebServer() {
  // send_P sends text stored in flash memory (PROGMEM), see web_page.h.
  web.on("/", []() { web.send_P(200, "text/html", WEB_PAGE); });   // 200 = "OK"

  web.on("/state", replyWithState);

  web.on("/power", []() {
    if (!web.hasArg("on")) return replyBadRequest("missing on=1|0");
    setPower(web.arg("on") != "0");
    replyWithState();
  });

  web.on("/colour", []() {
    CRGB colour;
    if (!colourFromText(web.arg("hex"), colour)) return replyBadRequest("hex must be RRGGBB or a colour name");
    setColour(colour);
    replyWithState();
  });

  web.on("/brightness", []() {
    long level = web.arg("value").toInt();   // toInt() gives 0 if the text isn't a number
    if (level < 1 || level > 255) return replyBadRequest("value must be 1-255");
    setBrightness(level);
    replyWithState();
  });

  web.on("/mode", []() {
    long id = web.arg("id").toInt();
    if (id < MODE_MIN || id > MODE_MAX) return replyBadRequest("id must be a mode number (1-14)");
    setMode(id);
    replyWithState();
  });

  web.on("/palette", []() {
    if (!web.hasArg("id")) return replyBadRequest("missing id");
    long id = web.arg("id").toInt();
    if (id < 0 || id >= PALETTE_COUNT) return replyBadRequest("unknown palette id");
    setPalette(id);
    replyWithState();
  });

  web.on("/temperature", []() {
    long kelvin = web.arg("kelvin").toInt();
    if (kelvin < MIN_KELVIN || kelvin > MAX_KELVIN) return replyBadRequest("kelvin must be 2000-6500");
    setColourTemperature(kelvin);
    replyWithState();
  });

  web.on("/speed", []() {
    long speed = web.arg("value").toInt();
    if (speed < SPEED_MIN || speed > SPEED_MAX) return replyBadRequest("value must be 1-10");
    setSpeed(speed);
    replyWithState();
  });

  web.on("/sleep", []() {
    if (!web.hasArg("minutes")) return replyBadRequest("missing minutes");
    long minutes = web.arg("minutes").toInt();
    if (minutes < 0 || minutes > SLEEP_MAX_MINUTES) return replyBadRequest("minutes must be 0-120");
    setSleepTimer(minutes);
    replyWithState();
  });

  // Any other address: first give Espalexa a chance, because the Echo's
  // requests (/api/...) arrive here. If it isn't one of those, answer with
  // the standard "404 Not Found".
  web.onNotFound([]() {
#if ENABLE_ALEXA
    if (espalexa.handleAlexaApiCall(web.uri(), web.arg(0))) return;   // arg(0) = the request body
#endif
    web.send(404, "text/plain", "Not found\n");
  });

  startAlexa();   // starts the web server too (see section 9)
}

void replyWithState() {
  web.sendHeader("Cache-Control", "no-store");   // tell the browser not to reuse an old answer
  web.send(200, "application/json", stateAsJson());
}

void replyBadRequest(const char *reason) {
  web.send(400, "text/plain", String(reason) + "\n");   // 400 = "Bad Request"
}

// Builds the JSON answer by hand, for example:
//   {"on":true,"mode":3,"palette":1,"brightness":128,"colour":"#ffb46b",...}
// Inside C text, \" is a quote mark, because a plain " would end the text.
String stateAsJson() {
  char colour[8];
  snprintf(colour, sizeof(colour), "#%02x%02x%02x", lamp.colour.r, lamp.colour.g, lamp.colour.b);

  String json;
  json.reserve(450);
  json += "{\"on\":";
  json += lamp.on ? "true" : "false";
  json += ",\"mode\":";       json += lamp.mode;
  json += ",\"palette\":";    json += lamp.palette;
  json += ",\"brightness\":"; json += lamp.brightness;
  json += ",\"speed\":";      json += lamp.speed;
  json += ",\"kelvin\":";     json += lamp.kelvin;
  json += ",\"sleep\":";      json += sleepMinutesLeft();
  json += ",\"colour\":\"";   json += colour;
  json += "\",\"palettes\":[";
  for (uint8_t i = 0; i < PALETTE_COUNT; i++) {
    if (i) json += ',';   // a comma between names, but not before the first
    json += '"';
    json += PALETTES[i].name;
    json += '"';
  }
  json += "],\"name\":\"";    json += LAMP_NAME;
  json += "\",\"host\":\"";   json += DEVICE_NAME;
  json += "\",\"ip\":\"";     json += WiFi.localIP().toString();
  json += "\",\"rssi\":";     json += WiFi.RSSI();
  json += ",\"mqtt\":";       json += mqtt.connected() ? "true" : "false";
  json += ",\"broker\":\"";  json += MQTT_HOST; json += "\"";
  json += ",\"heap\":";       json += ESP.getFreeHeap();
#if ENABLE_ALEXA
  json += ",\"alexa\":\"";    json += LAMP_NAME; json += "\"";
#endif
  json += ",\"version\":\"";  json += FIRMWARE_VERSION;
  json += "\",\"uptime\":\""; json += uptimeText();
  json += "\"}";
  return json;
}
