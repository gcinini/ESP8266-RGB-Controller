/*
  ESP8266 RGB Controller  v3.5.0

  Wi-Fi controller for a WS2812B LED lamp on a Wemos D1 Mini (ESP8266).
  Control it from its own web page, by voice through an Amazon Echo (Alexa),
  or through Adafruit IO (MQTT).

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
    6. Drawing                            (turning the state into LED colours)
    7. Saving settings                    (so the lamp remembers after a power cut)
    8. Wi-Fi, OTA and Adafruit IO         (networking)
    9. Alexa                              (voice control through an Echo)
   10. Web interface                      (the web page and its small API)

  THE BIG IDEA
  ------------
  A microcontroller can only do one thing at a time. If the code waited
  (with delay()) for an animation to finish, the lamp could not answer the
  web page or Adafruit IO in the meantime.

  So nothing here ever waits for long. Instead:
    * web requests, Alexa commands and MQTT messages only CHANGE THE STATE
      (the `lamp` variable) and return straight away;
    * loop() then draws ONE animation frame from that state, and comes back
      around a few microseconds later to check the network again.
  This is called "non-blocking" code, and it is the most important pattern in
  embedded programming.

  WHERE THE SETTINGS ARE
  ----------------------
    config.h    LED count, pin, device name, feed group... (one block per lamp)
    secrets.h   Wi-Fi and Adafruit IO passwords (not on GitHub)
*/


// ===========================================================================
// 1. Libraries and settings
// ===========================================================================
// #include copies another file into this one before compiling. Files in
// <angle brackets> come from installed libraries; files in "quotes" are in
// this sketch's own folder.

#include <ESP8266WiFi.h>        // joining a Wi-Fi network
#include <WiFiClientSecure.h>   // encrypted (TLS) network connections
#include <ESP8266WebServer.h>   // serving the web page
#include <ArduinoOTA.h>         // receiving firmware updates over Wi-Fi
#include <EEPROM.h>             // saving settings so they survive a power cut
#include <PubSubClient.h>       // MQTT, the messaging protocol Adafruit IO uses
#include <time.h>               // the clock (needed to check certificate dates)

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
// such as MQTT_USE_TLS. __has_include checks whether the file exists, so a
// missing secrets.h gives a clear message instead of a confusing error.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found: copy secrets.example.h to secrets.h and fill in your credentials"
#endif

#include "config.h"             // this lamp's settings
#include "web_page.h"           // the web page (HTML, CSS and JavaScript as one text)
#include "mqtt_roots.h"         // certificates trusted for the encrypted connection

// A `const` variable can't be changed after it is created. The compiler
// stops you if you try. `const char *` is a pointer to text: in C, text is
// stored as a row of characters, and the variable holds where it starts.
const char *FIRMWARE_VERSION = "3.5.0";

#if MQTT_USE_TLS
const uint16_t MQTT_PORT = 8883;   // encrypted MQTT
#else
const uint16_t MQTT_PORT = 1883;   // unencrypted MQTT
#endif

// A clock reading earlier than this means "not set yet" (the ESP8266 starts
// counting from 1 January 1970). 1704067200 seconds = 1 January 2024.
const time_t CLOCK_VALID_AFTER = 1704067200;


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
const uint8_t MODE_MIN = MODE_SOLID;
const uint8_t MODE_MAX = MODE_CANDLE;

// An array is a numbered list. MODE_NAMES[3] is "Rainbow". Index 0 is
// unused, so the index matches the mode number. `const char *const` means
// neither the list nor the text in it can be changed.
const char *const MODE_NAMES[] = {
  "", "Solid colour", "Colour fade", "Rainbow", "Random", "Palette, smooth", "Palette, stepped", "Candle"
};

// A struct groups several values into one new type, like a row in a table.
// This one pairs a word that Adafruit IO can send with the mode it selects.
struct ModeWord {
  const char *word;
  uint8_t mode;
};
const ModeWord MODE_WORDS[] = {
  { "solid", MODE_SOLID },   { "fade", MODE_FADE },               { "rainbow", MODE_RAINBOW },
  { "random", MODE_RANDOM }, { "smooth", MODE_PALETTE_SMOOTH },   { "palette", MODE_PALETTE_SMOOTH },
  { "stepped", MODE_PALETTE_STEPPED },                            { "candle", MODE_CANDLE },
};

// --- Palettes -----------------------------------------------------------------
// A palette is a list of 16 colours that FastLED blends between. CloudColors_p
// and friends are built into FastLED. The `&` takes the *address* of each
// palette, so the table stores where the palette is rather than a copy of it.
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
};

// The one and only lamp, with its starting values (from config.h).
Lamp lamp = { true, MODE_SOLID, 0, CRGB(DEFAULT_RED, DEFAULT_GREEN, DEFAULT_BLUE), DEFAULT_BRIGHTNESS };


// --- Other global variables -----------------------------------------------------
// "Global" means declared outside any function, so every function can use it.
CRGB          leds[LED_COUNT];        // one colour per LED; FastLED sends this array to the strip
CRGBPalette16 activePalette;          // the palette the current mode is drawing with
TBlendType    activeBlend = LINEARBLEND;   // LINEARBLEND = smooth, NOBLEND = hard steps
CRGB          solidColour;            // lamp.colour after gamma correction (see lampChanged)
uint8_t       animationPhase = 0;     // how far an animation has moved; wraps 255 -> 0 by itself

// These are *objects*, a C++ idea: a variable that carries its own functions
// (called methods), used like web.send(...) or mqtt.connect(...).
ESP8266WebServer web(80);             // web server on port 80, the normal HTTP port
#if MQTT_USE_TLS
BearSSL::WiFiClientSecure mqttSocket;                     // encrypted network connection
BearSSL::X509List         mqttTrustedRoots(MQTT_ROOT_CERTS);  // certificates we trust
#else
WiFiClient                mqttSocket;                     // plain network connection
#endif
PubSubClient     mqtt(mqttSocket);    // MQTT client that talks over mqttSocket

#if ENABLE_ALEXA
Espalexa         espalexa;            // answers the Echo's searches and commands
// A pointer to the "bulb" Alexa sees. nullptr means "points to nothing yet";
// startAlexa() creates the device and stores its address here.
EspalexaDevice  *alexaLight = nullptr;
#endif

// Adafruit IO "topics" - the addresses of the feeds - built in setup().
// String is a C++ class for text that can grow; it is easier than C text but
// uses more memory, so this sketch uses it only where it helps.
String topicPower, topicColour, topicBrightness, topicButton, topicStatus;

// Bookkeeping for the delayed jobs (saving, status reports).
bool     savePending = false;     // true = settings changed and need saving
uint32_t lastChangeAt = 0;        // millis() when the lamp last changed
bool     statusPending = true;    // true = Adafruit IO should get a status report
uint32_t lastStatusAt = 0;        // millis() of the last report (0 = never)


// ===========================================================================
// Function declarations ("prototypes")
// ===========================================================================
// In C, a function must be declared before it is used. These lines tell the
// compiler each function's name, inputs and result, so the functions can be
// written in any order below. `void` means "returns nothing".

// Lamp control (used by both the web page and Adafruit IO)
void setPower(bool on);
void setMode(uint8_t mode);
void setPalette(uint8_t id);
void setColour(CRGB colour);
void setBrightness(uint8_t level);
void lampChanged();
void preparePalette();
bool colourFromText(String text, CRGB &out);

// Drawing
void drawFrame();
void drawPaletteAlongStrip();
void drawCandle();
void pickRandomPalette();

// Status reports
String statusText();
String uptimeText();
void publishStatusIfDue();

// Settings in flash
void loadSettings();
void saveSettingsIfDue();

// Networking
void connectWiFi();
void startOta();
bool clockIsSet();
void prepareMqttSocket();
void serviceMqtt();
void onMqttMessage(char *topic, byte *payload, unsigned int length);

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
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);   // start setting the clock (UTC) in the background
  startOta();
  startWebServer();

  // Build the Adafruit IO feed addresses, e.g. "gcinini/feeds/espweb.onoff".
  // Adding Strings with + joins them together.
  String feeds = String(MQTT_USER) + "/feeds/" + FEED_GROUP;
  topicPower      = feeds + ".onoff";
  topicColour     = feeds + ".color";
  topicBrightness = feeds + ".brightness";
  topicButton     = feeds + ".button";
  topicStatus     = feeds + ".outtopic";
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);   // "call onMqttMessage() whenever a message arrives"
  mqtt.setBufferSize(512);           // room for the status report (the default is 256 bytes)
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
  serviceMqtt();           // any Adafruit IO messages? reconnect if needed
  drawFrame();             // draw the next animation frame, if it's time
  saveSettingsIfDue();     // save settings, if it's time
  publishStatusIfDue();    // send a status report, if it's time
}


// ===========================================================================
// 4. Lamp control
// ===========================================================================
// Every change to the lamp goes through one of these functions, whether it
// came from the web page or from Adafruit IO. Each one updates `lamp`, prints
// a line to the serial monitor, and calls lampChanged().

void setPower(bool on) {
  lamp.on = on;
  // `condition ? a : b` is the ternary operator: a if true, b if false.
  Serial.println(on ? "Lamp on" : "Lamp off");
  lampChanged();
}

void setMode(uint8_t mode) {
  if (mode < MODE_MIN || mode > MODE_MAX) return;   // || means "or"; ignore unknown modes
  lamp.mode = mode;
  lamp.on = true;
  Serial.printf("Mode %s\n", MODE_NAMES[mode]);   // %s in printf is replaced by text
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
  lamp.mode = MODE_SOLID;
  lamp.on = true;
  // %02X prints a number as 2 hexadecimal digits, so 255 becomes FF.
  Serial.printf("Colour #%02X%02X%02X\n", colour.r, colour.g, colour.b);
  lampChanged();
}

void setBrightness(uint8_t level) {
  lamp.brightness = (level == 0) ? 1 : level;   // never 0, so "on" always shows something
  Serial.printf("Brightness %u\n", lamp.brightness);   // %u = unsigned whole number
  lampChanged();
}

// Called after every change: applies it and schedules the delayed jobs.
void lampChanged() {
  FastLED.setBrightness(lamp.brightness);

  // Gamma correction. Our eyes don't see brightness in a straight line: an
  // LED at half power looks much brighter than "half". applyGamma_video()
  // bends the numbers so colours picked on a screen look right on the LEDs.
  solidColour = CRGB(applyGamma_video(lamp.colour.r, COLOUR_GAMMA),
                     applyGamma_video(lamp.colour.g, COLOUR_GAMMA),
                     applyGamma_video(lamp.colour.b, COLOUR_GAMMA));
  preparePalette();
  syncAlexa();   // so the Alexa app shows what the lamp is really doing

  // Don't save or report right now. Just note that it needs doing; loop()
  // does it a moment later (see saveSettingsIfDue and publishStatusIfDue).
  savePending = true;
  statusPending = true;
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


// ===========================================================================
// 6. Drawing - one frame per call, at most every FRAME_INTERVAL_MS
// ===========================================================================
// A `static` variable inside a function keeps its value between calls
// (normal local variables start fresh every time). That's how drawFrame()
// remembers when it last drew a frame.
void drawFrame() {
  static uint32_t lastFrameAt = 0;
  static uint32_t frameNumber = 0;
  static uint32_t randomSince = 0;

  // The non-blocking timer: "has FRAME_INTERVAL_MS passed since the last
  // frame?" If not, return immediately and let loop() do other work.
  // Writing it as `now - lastFrameAt` (not `now > lastFrameAt + interval`)
  // keeps it correct even when millis() rolls over to 0 after 49.7 days,
  // because unsigned subtraction wraps around too.
  uint32_t now = millis();
  if (now - lastFrameAt < FRAME_INTERVAL_MS) return;
  lastFrameAt = now;
  frameNumber++;   // ++ adds 1

  if (!lamp.on) {
    FastLED.clear();   // all LEDs black...
    FastLED.show();    // ...and send that to the strip
    return;
  }

  switch (lamp.mode) {
    case MODE_SOLID:
      fill_solid(leds, LED_COUNT, solidColour);
      break;

    case MODE_FADE:
      // One colour for the whole lamp, moving one palette step every 4 frames
      // (about 20 s per cycle). % is the remainder after dividing, so
      // frameNumber % 4 == 0 is true on every 4th frame.
      if (frameNumber % 4 == 0) animationPhase++;
      fill_solid(leds, LED_COUNT, ColorFromPalette(activePalette, animationPhase, 255, activeBlend));
      break;

    case MODE_RANDOM:
      if (now - randomSince >= RANDOM_HOLD_MS) {   // time for a new palette?
        randomSince = now;
        pickRandomPalette();
      }
      animationPhase++;
      drawPaletteAlongStrip();
      break;

    case MODE_CANDLE:
      drawCandle();
      break;

    default:   // rainbow and the two palette modes
      animationPhase++;
      drawPaletteAlongStrip();
      break;
  }
  FastLED.show();   // nothing appears on the strip until show() sends the leds[] array
}

// Spreads the active palette along the strip, shifted by the animation phase.
// Because animationPhase grows by 1 every frame, the colours appear to move.
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

  noiseClock += 7;                     // += adds to the variable: noiseClock = noiseClock + 7
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
const uint8_t SETTINGS_MARKER = 0xC1;   // 0x means hexadecimal: 0xC1 = 193

struct StoredSettings {
  uint8_t marker;
  uint8_t mode, palette, brightness;
  uint8_t red, green, blue;
};

void loadSettings() {
  EEPROM.begin(sizeof(StoredSettings));   // reserve exactly as many bytes as we store
  StoredSettings s = {};                  // = {} starts every field at 0
  EEPROM.get(0, s);                       // copy the saved bytes into s
  if (s.marker == SETTINGS_MARKER) {
    // Only accept values that make sense; a garbled byte keeps the default.
    if (s.mode >= MODE_MIN && s.mode <= MODE_MAX) lamp.mode = s.mode;
    if (s.palette < PALETTE_COUNT) lamp.palette = s.palette;
    if (s.brightness > 0) lamp.brightness = s.brightness;
    lamp.colour = CRGB(s.red, s.green, s.blue);
    Serial.println("Settings restored");
  } else {
    Serial.println("No saved settings - using defaults");
  }
  lamp.on = true;
  lampChanged();
  savePending = false;   // nothing new to save yet
}

void saveSettingsIfDue() {
  if (!savePending || millis() - lastChangeAt < SAVE_DELAY_MS) return;
  savePending = false;

  StoredSettings wanted = { SETTINGS_MARKER, lamp.mode, lamp.palette, lamp.brightness,
                            lamp.colour.r, lamp.colour.g, lamp.colour.b };
  StoredSettings current = {};
  EEPROM.get(0, current);
  // memcmp compares two blocks of memory byte by byte; 0 means identical.
  if (memcmp(&wanted, &current, sizeof(StoredSettings)) == 0) return;   // unchanged: skip the write

  EEPROM.put(0, wanted);
  EEPROM.commit();   // actually writes to flash
  Serial.println("Settings saved");
}


// ===========================================================================
// 8. Wi-Fi, OTA and Adafruit IO
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
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA update finished - restarting"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA update failed, error %u\n", error); });
  ArduinoOTA.begin();
}

bool clockIsSet() {
  // time(nullptr) returns the current time in seconds since 1 January 1970.
  return time(nullptr) > CLOCK_VALID_AFTER;
}

// Sets up the encrypted connection once: which certificates to trust, and the
// smallest TLS buffers the server allows (saves RAM on the ESP8266).
void prepareMqttSocket() {
#if MQTT_USE_TLS
  static bool prepared = false;
  if (prepared) return;
  prepared = true;

  mqttSocket.setTrustAnchors(&mqttTrustedRoots);
  // :: reaches inside a class. probeMaxFragmentLength asks the server whether
  // it accepts small 1 KB message pieces instead of the standard 16 KB.
  if (BearSSL::WiFiClientSecure::probeMaxFragmentLength(MQTT_HOST, MQTT_PORT, 1024)) {
    mqttSocket.setBufferSizes(1024, 1024);
    Serial.println("Adafruit IO: server supports small TLS buffers (1 KB)");
  } else {
    Serial.println("Adafruit IO: using standard TLS buffers (16 KB)");
  }
#endif
}

// Keeps the Adafruit IO connection alive. When it is down, reconnect attempts
// start every MQTT_RETRY_MS and back off to once a minute if they keep failing
// (each encrypted attempt pauses the lamp for a second or two). The lamp and
// the web page keep working in between.
void serviceMqtt() {
  if (mqtt.connected()) {
    mqtt.loop();   // receive messages (which calls onMqttMessage) and keep the connection alive
    return;
  }

  static uint32_t lastAttemptAt = 0;
  static uint32_t retryDelay = 0;          // 0 = try straight away
  if (millis() - lastAttemptAt < retryDelay) return;
  if (WiFi.status() != WL_CONNECTED) return;

#if MQTT_USE_TLS
  static bool waitingForClock = false;
  if (!clockIsSet()) {                     // certificate dates can't be checked yet
    if (!waitingForClock) Serial.println("Adafruit IO: waiting for the clock to be set by NTP");
    waitingForClock = true;
    return;
  }
  if (waitingForClock) Serial.println("Adafruit IO: clock set");
  waitingForClock = false;
  prepareMqttSocket();
  mqttSocket.setX509Time(time(nullptr));   // tell the certificate checker today's date
#endif

  lastAttemptAt = millis();
  // The client ID must be unique on Adafruit IO, so add the chip's serial number.
  String clientId = String(DEVICE_NAME) + "-" + String(ESP.getChipId(), HEX);
  Serial.printf("Adafruit IO: connecting to %s:%u%s... ", MQTT_HOST, MQTT_PORT, MQTT_USE_TLS ? " (TLS)" : "");
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_KEY)) {
    Serial.printf("connected (free memory %u bytes)\n", ESP.getFreeHeap());
    // .c_str() turns a String into plain C text, which the library expects.
    mqtt.subscribe(topicPower.c_str());
    mqtt.subscribe(topicColour.c_str());
    mqtt.subscribe(topicBrightness.c_str());
    mqtt.subscribe(topicButton.c_str());
    statusPending = true;   // report the current state straight away
    lastStatusAt = 0;
    retryDelay = MQTT_RETRY_MS;
    return;
  }

  Serial.printf("failed (state %d)\n", mqtt.state());   // %d = whole number that may be negative
#if MQTT_USE_TLS
  char reason[80];   // a C text buffer: room for 79 characters plus the end marker
  int sslError = mqttSocket.getLastSSLError(reason, sizeof(reason));
  if (sslError != 0) {
    Serial.printf("  TLS error %d: %s\n", sslError, reason);
    Serial.println("  (a certificate error usually means Adafruit IO changed certificate authority - see mqtt_roots.h)");
  }
#endif
  // Back off: wait longer after each failure, up to a minute.
  if (retryDelay == 0)          retryDelay = MQTT_RETRY_MS;
  else if (retryDelay < 30000)  retryDelay *= 2;   // *= doubles it: retryDelay = retryDelay * 2
  else                          retryDelay = 60000;
  Serial.printf("  retrying in %lu s\n", (unsigned long)(retryDelay / 1000));
}

// Called by the MQTT library whenever a message arrives on a feed we
// subscribed to. The message arrives as raw bytes (payload) with a length,
// not as ready-made text, so the first job is to copy it into a String.
//
// Feeds (with FEED_GROUP from config.h, e.g. "espweb"):
//   <group>.brightness  0-100
//   <group>.color       colour name ("Blue", "Hot Pink") or hex ("#FF8800")
//   <group>.onoff and <group>.button
//                       ON, OFF, WHITE,
//                       SOLID, FADE, RAINBOW, RANDOM, SMOOTH (or PALETTE), STEPPED, CANDLE,
//                       or a palette name: CLOUD, OCEAN, PARTY, HEAT, FOREST, LAVA
//   <group>.outtopic    (published by the lamp) status report
void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  String feed(topic);
  String text;
  text.reserve(length);   // set aside memory once, rather than growing it letter by letter
  for (unsigned int i = 0; i < length; i++) text += (char)payload[i];   // (char) treats each byte as a letter
  text.trim();
  Serial.printf("Adafruit IO: %s = %s\n", feed.c_str(), text.c_str());

  if (feed == topicBrightness) {
    // constrain() keeps a value inside a range; map() rescales 0-100 to 1-255.
    long percent = constrain(text.toInt(), 0L, 100L);
    setBrightness(map(percent, 0, 100, 1, 255));
    return;
  }

  if (feed == topicColour) {
    CRGB colour;
    if (colourFromText(text, colour)) setColour(colour);
    else Serial.println("  unknown colour");
    return;
  }

  // Everything else is a command word. `return setPower(true);` calls the
  // function and then leaves onMqttMessage, so only one command runs.
  text.toLowerCase();
  if (text == "on")    return setPower(true);
  if (text == "off")   return setPower(false);
  if (text == "white") return setColour(CRGB::White);

  for (const ModeWord &m : MODE_WORDS) {
    if (text == m.word) return setMode(m.mode);
  }
  for (uint8_t i = 0; i < PALETTE_COUNT; i++) {
    if (text.equalsIgnoreCase(PALETTES[i].name)) return setPalette(i);
  }
  Serial.println("  unknown command");
}

// The report shown in the dashboard's status block. "\n" starts a new line.
String statusText() {
  String s;
  s.reserve(300);
  s += "Power: ";       s += lamp.on ? "On" : "Off";
  s += "\nMode: ";      s += MODE_NAMES[lamp.mode];
  if (lamp.mode == MODE_PALETTE_SMOOTH || lamp.mode == MODE_PALETTE_STEPPED) {
    s += "\nPalette: "; s += PALETTES[lamp.palette].name;
  }
  if (lamp.mode == MODE_SOLID) {
    char hex[8];   // "#RRGGBB" is 7 characters, plus 1 for the end-of-text marker
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", lamp.colour.r, lamp.colour.g, lamp.colour.b);
    s += "\nColour: ";  s += hex;
  }
  // + 127 rounds to the nearest percent instead of always rounding down
  s += "\nBrightness: "; s += (lamp.brightness * 100 + 127) / 255; s += "%";
  s += "\nWi-Fi: ";     s += WiFi.SSID(); s += " ("; s += WiFi.RSSI(); s += " dBm)";
  s += "\nIP: ";        s += WiFi.localIP().toString();
  s += "\nHost: ";      s += DEVICE_NAME; s += ".local";
#if ENABLE_ALEXA
  s += "\nAlexa name: "; s += ALEXA_NAME;
#endif
  s += "\nAdafruit IO: "; s += MQTT_USE_TLS ? "encrypted (TLS)" : "unencrypted";
  s += "\nFirmware: v"; s += FIRMWARE_VERSION; s += ", up "; s += uptimeText();
  return s;
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

// Sends the status report after a change has settled, never more often than
// STATUS_MIN_GAP_MS, and every STATUS_HEARTBEAT_MS so the uptime stays current.
void publishStatusIfDue() {
  if (!mqtt.connected()) return;
  uint32_t now = millis();
  bool heartbeatDue = (now - lastStatusAt >= STATUS_HEARTBEAT_MS);
  if (!statusPending && !heartbeatDue) return;
  if (now - lastChangeAt < STATUS_SETTLE_MS) return;
  if (lastStatusAt != 0 && now - lastStatusAt < STATUS_MIN_GAP_MS) return;

  if (mqtt.publish(topicStatus.c_str(), statusText().c_str())) {
    statusPending = false;
    lastStatusAt = (now == 0) ? 1 : now;   // never store 0, which means "never sent"
  }
}


// ===========================================================================
// 9. Alexa
// ===========================================================================
// How it works:
//   1. "Alexa, discover devices": the Echo sends a search message (SSDP) to
//      every device on the local network. Espalexa answers "I am a Hue bridge".
//   2. The Echo asks for the bridge's description (/description.xml) and its
//      list of lights (/api/.../lights), and finds one colour light,
//      ALEXA_NAME. These requests arrive on our own web server (port 80).
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
  alexaLight = new EspalexaDevice(ALEXA_NAME, onAlexaCommand, EspalexaDeviceType::color, lamp.brightness);
  espalexa.addDevice(alexaLight);
  syncAlexa();                 // start with the lamp's real state
  // Share our web server with Espalexa. begin() adds its pages, starts
  // listening for Echo searches, and then starts the web server. It returns
  // false if it could not listen for searches - and in that case it does NOT
  // start the web server, so we start it ourselves to keep the web page working.
  if (espalexa.begin(&web)) {
    Serial.printf("Alexa: ready as \"%s\" - say \"Alexa, discover devices\"\n", ALEXA_NAME);
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

  // A colour request: hs = hue and saturation ("blue"), ct = colour
  // temperature ("warm white"), xy = a colour chart position. Espalexa turns
  // all three into ordinary red, green and blue values for us.
  if (changed == EspalexaDeviceProperty::hs || changed == EspalexaDeviceProperty::ct ||
      changed == EspalexaDeviceProperty::xy) {
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
// Adafruit IO. Called from lampChanged(); does nothing without Alexa.
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
//   /mode?id=1-7
//   /palette?id=0-5
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
    if (id < MODE_MIN || id > MODE_MAX) return replyBadRequest("id must be 1-7");
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
  json.reserve(400);
  json += "{\"on\":";
  json += lamp.on ? "true" : "false";
  json += ",\"mode\":";       json += lamp.mode;
  json += ",\"palette\":";    json += lamp.palette;
  json += ",\"brightness\":"; json += lamp.brightness;
  json += ",\"colour\":\"";   json += colour;
  json += "\",\"palettes\":[";
  for (uint8_t i = 0; i < PALETTE_COUNT; i++) {
    if (i) json += ',';   // a comma between names, but not before the first
    json += '"';
    json += PALETTES[i].name;
    json += '"';
  }
  json += "],\"host\":\"";    json += DEVICE_NAME;
  json += "\",\"ip\":\"";     json += WiFi.localIP().toString();
  json += "\",\"rssi\":";     json += WiFi.RSSI();
  json += ",\"mqtt\":";       json += mqtt.connected() ? "true" : "false";
  json += ",\"tls\":";        json += MQTT_USE_TLS ? "true" : "false";
  json += ",\"heap\":";       json += ESP.getFreeHeap();
#if ENABLE_ALEXA
  json += ",\"alexa\":\"";    json += ALEXA_NAME; json += "\"";
#endif
  json += ",\"version\":\"";  json += FIRMWARE_VERSION;
  json += "\",\"uptime\":\""; json += uptimeText();
  json += "\"}";
  return json;
}
