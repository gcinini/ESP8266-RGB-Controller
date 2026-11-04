/*
  ESP8266 RGB Controller  v3.3.0

  Wi-Fi controller for a WS2812B LED lamp on a Wemos D1 Mini (ESP8266).
  Control it from the built-in web page or through Adafruit IO (MQTT).

  Copyright (c) 2019-2026 G Cinini
  SPDX-License-Identifier: MIT
  See LICENSE, and THIRD_PARTY_NOTICES.md for the libraries this sketch uses.

  Design
  ------
  Everything that changes the lamp (web requests, MQTT messages) only updates the
  `lamp` state below and returns. loop() then draws one animation frame from that
  state. Settings are written to flash a couple of seconds after the last change,
  so dragging the brightness slider does not wear out the flash.
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <time.h>

#define FASTLED_ESP8266_RAW_PIN_ORDER   // must come before FastLED.h
#include <FastLED.h>

#include "web_page.h"
#include "mqtt_roots.h"

// Wi-Fi and Adafruit IO credentials live in secrets.h, which git ignores.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found: copy secrets.example.h to secrets.h and fill in your credentials"
#endif


// ===========================================================================
// Configuration
// ===========================================================================
#define LED_PIN    D4         // data line to the strip
#define LED_COUNT  35         // number of LEDs on the strip
#define LED_TYPE   WS2812B
#define LED_ORDER  GRB

const char    *FIRMWARE_VERSION = "3.3.0";
const char    *DEVICE_NAME     = "ESP-lightbox1";   // network / OTA name
const uint32_t FRAME_INTERVAL  = 20;            // ms per animation frame (50 fps)
const uint32_t MQTT_RETRY_MS   = 5000;          // wait between MQTT reconnect attempts
const uint32_t SAVE_DELAY_MS   = 2000;          // write settings this long after the last change
const uint32_t RANDOM_HOLD_MS  = 8000;          // how long Random mode keeps each palette
const float    COLOUR_GAMMA    = 2.6f;          // makes picked colours look right on LEDs

// Candle mode lights only the bottom of the lamp, like a tea light in a jar.
const uint8_t  CANDLE_LEDS       = 14;          // how many LEDs form the flame
const bool     CANDLE_AT_LED_ZERO = true;       // true if LED 0 is at the bottom of the lamp

// Status reports to Adafruit IO (feed espweb.outtopic). The free plan allows
// 30 messages a minute, so reports are grouped and spaced out.
const uint32_t STATUS_SETTLE_MS   = 1500;       // wait for changes to settle before reporting
const uint32_t STATUS_MIN_GAP_MS  = 5000;       // never report more often than this
const uint32_t STATUS_HEARTBEAT_MS = 600000;    // report every 10 minutes even without changes

// Adafruit IO connection. With MQTT_USE_TLS on (the default), the connection is
// encrypted and the server's certificate is checked against the root
// certificates in mqtt_roots.h. Add `#define MQTT_USE_TLS 0` to secrets.h to fall
// back to the unencrypted port 1883 (your Adafruit IO key is then sent in clear).
#ifndef MQTT_USE_TLS
#define MQTT_USE_TLS 1
#endif

const char *MQTT_HOST = "io.adafruit.com";
#if MQTT_USE_TLS
const uint16_t MQTT_PORT = 8883;
#else
const uint16_t MQTT_PORT = 1883;
#endif

// Certificates can only be checked with the correct date, so the lamp sets its
// clock from these NTP servers before connecting.
const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.google.com";
const time_t CLOCK_VALID_AFTER = 1704067200;   // 2024-01-01: anything earlier means "not set yet"


// ===========================================================================
// Lamp state
// ===========================================================================
// Mode numbers are used by the web API and stored in flash.
const uint8_t MODE_SOLID           = 1;   // one colour
const uint8_t MODE_FADE            = 2;   // whole lamp slowly drifts through colours
const uint8_t MODE_RAINBOW         = 3;   // rainbow moving along the strip
const uint8_t MODE_RANDOM          = 4;   // a different palette every few seconds
const uint8_t MODE_PALETTE_SMOOTH  = 5;   // chosen palette, blended
const uint8_t MODE_PALETTE_STEPPED = 6;   // chosen palette, hard steps
const uint8_t MODE_CANDLE          = 7;   // flickering candle at the bottom of the lamp
const uint8_t MODE_MIN = MODE_SOLID;
const uint8_t MODE_MAX = MODE_CANDLE;

// Display names (index = mode number) and the words MQTT accepts for each mode.
const char *const MODE_NAMES[] = {
  "", "Solid colour", "Colour fade", "Rainbow", "Random", "Palette, smooth", "Palette, stepped", "Candle"
};

struct ModeWord {
  const char *word;
  uint8_t mode;
};
const ModeWord MODE_WORDS[] = {
  { "solid", MODE_SOLID },   { "fade", MODE_FADE },               { "rainbow", MODE_RAINBOW },
  { "random", MODE_RANDOM }, { "smooth", MODE_PALETTE_SMOOTH },   { "palette", MODE_PALETTE_SMOOTH },
  { "stepped", MODE_PALETTE_STEPPED },                            { "candle", MODE_CANDLE },
};

struct NamedPalette {
  const char *name;
  const TProgmemRGBPalette16 *colours;
};

// Palettes offered on the web page (index = palette id).
const NamedPalette PALETTES[] = {
  { "Cloud",  &CloudColors_p  },
  { "Ocean",  &OceanColors_p  },
  { "Party",  &PartyColors_p  },
  { "Heat",   &HeatColors_p   },
  { "Forest", &ForestColors_p },
  { "Lava",   &LavaColors_p   },
};
const uint8_t PALETTE_COUNT = sizeof(PALETTES) / sizeof(PALETTES[0]);

struct Lamp {
  bool    on;
  uint8_t mode;
  uint8_t palette;
  CRGB    colour;       // as picked (gamma is applied when drawing)
  uint8_t brightness;   // 1-255, applied through FastLED.setBrightness()
};

Lamp lamp = { true, MODE_SOLID, 0, CRGB(255, 180, 107), 128 };   // warm white, half brightness


// ===========================================================================
// Globals
// ===========================================================================
CRGB          leds[LED_COUNT];
CRGBPalette16 activePalette;
TBlendType    activeBlend = LINEARBLEND;
CRGB          solidColour;            // lamp.colour after gamma correction
uint8_t       animationPhase = 0;

ESP8266WebServer web(80);
#if MQTT_USE_TLS
BearSSL::WiFiClientSecure mqttSocket;
BearSSL::X509List         mqttTrustedRoots(MQTT_ROOT_CERTS);
#else
WiFiClient                mqttSocket;
#endif
PubSubClient     mqtt(mqttSocket);

// Adafruit IO feed topics, built from MQTT_USER in setup()
String topicPower, topicColour, topicBrightness, topicButton, topicStatus;

bool     savePending = false;
uint32_t lastChangeAt = 0;

bool     statusPending = true;    // send a report as soon as Adafruit IO connects
uint32_t lastStatusAt = 0;


// ===========================================================================
// Function declarations
// ===========================================================================
// Lamp control (shared by the web page and MQTT)
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

// Status
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

// Web interface
void startWebServer();
void replyWithState();
void replyBadRequest(const char *reason);
String stateAsJson();


// ===========================================================================
// ========     setup() and loop()                                     =======
// ===========================================================================
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.printf("ESP8266 RGB Controller v%s\n", FIRMWARE_VERSION);

  FastLED.addLeds<LED_TYPE, LED_PIN, LED_ORDER>(leds, LED_COUNT).setCorrection(TypicalLEDStrip);
  FastLED.setDither(1);

  loadSettings();         // restore the last state so the lamp lights up immediately
  connectWiFi();          // keeps animating while it waits
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);   // UTC; the clock is only used for certificate checks
  startOta();
  startWebServer();

  topicPower      = String(MQTT_USER) + "/feeds/espweb.onoff";
  topicColour     = String(MQTT_USER) + "/feeds/espweb.color";
  topicBrightness = String(MQTT_USER) + "/feeds/espweb.brightness";
  topicButton     = String(MQTT_USER) + "/feeds/espweb.button";
  topicStatus     = String(MQTT_USER) + "/feeds/espweb.outtopic";
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);   // room for the status report (the default is 256 bytes)
}

void loop() {
  ArduinoOTA.handle();
  web.handleClient();
  serviceMqtt();
  drawFrame();
  saveSettingsIfDue();
  publishStatusIfDue();
}


// ===========================================================================
// Lamp control
// ===========================================================================
void setPower(bool on) {
  lamp.on = on;
  Serial.println(on ? "Lamp on" : "Lamp off");
  lampChanged();
}

void setMode(uint8_t mode) {
  if (mode < MODE_MIN || mode > MODE_MAX) return;
  lamp.mode = mode;
  lamp.on = true;
  Serial.printf("Mode %s\n", MODE_NAMES[mode]);
  lampChanged();
}

void setPalette(uint8_t id) {
  if (id >= PALETTE_COUNT) return;
  lamp.palette = id;
  // Show the palette straight away, keeping "stepped" if that is what was showing
  if (lamp.mode != MODE_PALETTE_STEPPED) lamp.mode = MODE_PALETTE_SMOOTH;
  lamp.on = true;
  Serial.printf("Palette %s\n", PALETTES[id].name);
  lampChanged();
}

void setColour(CRGB colour) {
  lamp.colour = colour;
  lamp.mode = MODE_SOLID;
  lamp.on = true;
  Serial.printf("Colour #%02X%02X%02X\n", colour.r, colour.g, colour.b);
  lampChanged();
}

void setBrightness(uint8_t level) {
  lamp.brightness = (level == 0) ? 1 : level;
  Serial.printf("Brightness %u\n", lamp.brightness);
  lampChanged();
}

// Applies the new state to the LEDs and schedules a save.
void lampChanged() {
  FastLED.setBrightness(lamp.brightness);
  solidColour = CRGB(applyGamma_video(lamp.colour.r, COLOUR_GAMMA),
                     applyGamma_video(lamp.colour.g, COLOUR_GAMMA),
                     applyGamma_video(lamp.colour.b, COLOUR_GAMMA));
  preparePalette();
  savePending = true;
  statusPending = true;
  lastChangeAt = millis();
}

// Loads the palette and blending the current mode draws with.
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
      activePalette = *PALETTES[lamp.palette].colours;
      activeBlend = (lamp.mode == MODE_PALETTE_SMOOTH) ? LINEARBLEND : NOBLEND;
      break;
    default:
      break;
  }
}


// ===========================================================================
// Colour names and hex values
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

// Understands colour names ("Hot Pink", "royalblue") and hex ("#FF8800" or "ff8800").
bool colourFromText(String text, CRGB &out) {
  text.trim();
  text.toLowerCase();
  text.replace(" ", "");
  for (const NamedColour &c : COLOUR_NAMES) {
    if (text == c.name) {
      out = CRGB(c.r, c.g, c.b);
      return true;
    }
  }
  if (text.startsWith("#")) text.remove(0, 1);
  if (text.length() != 6) return false;
  for (unsigned int i = 0; i < text.length(); i++) {
    if (!isxdigit(text[i])) return false;
  }
  uint32_t value = strtoul(text.c_str(), nullptr, 16);
  out = CRGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
  return true;
}


// ===========================================================================
// Drawing - one frame per call, at most every FRAME_INTERVAL ms
// ===========================================================================
void drawFrame() {
  static uint32_t lastFrameAt = 0;
  static uint32_t frameNumber = 0;
  static uint32_t randomSince = 0;

  uint32_t now = millis();
  if (now - lastFrameAt < FRAME_INTERVAL) return;
  lastFrameAt = now;
  frameNumber++;

  if (!lamp.on) {
    FastLED.clear();
    FastLED.show();
    return;
  }

  switch (lamp.mode) {
    case MODE_SOLID:
      fill_solid(leds, LED_COUNT, solidColour);
      break;

    case MODE_FADE:
      // One colour for the whole lamp, moving one palette step every 4 frames (~20 s per cycle)
      if (frameNumber % 4 == 0) animationPhase++;
      fill_solid(leds, LED_COUNT, ColorFromPalette(activePalette, animationPhase, 255, activeBlend));
      break;

    case MODE_RANDOM:
      if (now - randomSince >= RANDOM_HOLD_MS) {
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
  FastLED.show();
}

// Spreads the active palette along the strip, shifted by the animation phase.
void drawPaletteAlongStrip() {
  const uint8_t spacing = 4;   // palette steps between neighbouring LEDs
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    leds[i] = ColorFromPalette(activePalette, animationPhase + i * spacing, 255, activeBlend);
  }
}

// Candle mode: a warm flame at the bottom of the lamp, brightest at its base and
// dimmer and redder towards the tip. Slow noise makes the whole flame breathe,
// faster noise makes each LED shimmer, and now and then a "draught" makes it dip.
void drawCandle() {
  static uint16_t noiseClock = 0;
  static uint8_t draught = 0;          // how far the flame is currently dipping

  noiseClock += 7;
  if (draught > 0) {
    draught = qsub8(draught, 5);       // recover over a few hundred milliseconds
  } else if (random8() == 0) {         // about once every 5 seconds at 50 fps
    draught = random8(60, 130);
  }

  uint8_t flame = qadd8(150, scale8(inoise8(noiseClock), 105));   // overall level 150-255
  flame = qsub8(flame, draught);

  const CRGB base(255, 147, 41);       // the yellow-orange of a candle flame
  const CRGB tip(255, 64, 6);          // deeper orange at the top of the flame

  FastLED.clear();
  for (uint8_t i = 0; i < CANDLE_LEDS && i < LED_COUNT; i++) {
    uint8_t height = (CANDLE_LEDS > 1) ? (uint16_t)i * 255 / (CANDLE_LEDS - 1) : 0;   // 0 = base, 255 = tip
    uint8_t shimmer = inoise8((uint16_t)i * 50, noiseClock * 3);

    uint8_t level = scale8(flame, 255 - scale8(height, 180));       // fade out towards the tip
    level = scale8(level, 190 + scale8(shimmer, 65));               // per-LED shimmer

    CRGB colour = blend(base, tip, height);
    colour.nscale8_video(level);

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

  if (random8(4) == 0) {
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
// Settings in flash (EEPROM emulation)
// ===========================================================================
const uint8_t SETTINGS_MARKER = 0xC1;   // changes whenever the layout below changes

struct StoredSettings {
  uint8_t marker;
  uint8_t mode, palette, brightness;
  uint8_t red, green, blue;
};

void loadSettings() {
  EEPROM.begin(sizeof(StoredSettings));
  StoredSettings s = {};
  EEPROM.get(0, s);
  if (s.marker == SETTINGS_MARKER) {
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
  if (memcmp(&wanted, &current, sizeof(StoredSettings)) == 0) return;   // unchanged, skip the flash write

  EEPROM.put(0, wanted);
  EEPROM.commit();
  Serial.println("Settings saved");
}


// ===========================================================================
// Wi-Fi, OTA and MQTT
// ===========================================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(DEVICE_NAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Joining Wi-Fi \"%s\"", WIFI_SSID);

  uint32_t lastDot = millis();
  while (WiFi.status() != WL_CONNECTED) {
    drawFrame();   // keep the lamp animating while we wait
    delay(5);
    if (millis() - lastDot >= 500) {
      lastDot = millis();
      Serial.print('.');
    }
  }
  Serial.printf("\nWi-Fi connected - http://%s/  (%s.local)\n",
                WiFi.localIP().toString().c_str(), DEVICE_NAME);
}

void startOta() {
  ArduinoOTA.setHostname(DEVICE_NAME);
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);   // optional, set in secrets.h
#endif
  ArduinoOTA.onStart([]() {
    Serial.println("OTA update starting");
    FastLED.clear(true);   // LEDs off while flashing
  });
  ArduinoOTA.onEnd([]() { Serial.println("OTA update finished - restarting"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA update failed, error %u\n", error); });
  ArduinoOTA.begin();
}

bool clockIsSet() {
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
// (an encrypted attempt pauses the lamp for a second or two). The lamp and the
// web page keep working in between.
void serviceMqtt() {
  if (mqtt.connected()) {
    mqtt.loop();
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
  mqttSocket.setX509Time(time(nullptr));
#endif

  lastAttemptAt = millis();
  String clientId = String(DEVICE_NAME) + "-" + String(ESP.getChipId(), HEX);
  Serial.printf("Adafruit IO: connecting to %s:%u%s... ", MQTT_HOST, MQTT_PORT, MQTT_USE_TLS ? " (TLS)" : "");
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_KEY)) {
    Serial.printf("connected (free memory %u bytes)\n", ESP.getFreeHeap());
    mqtt.subscribe(topicPower.c_str());
    mqtt.subscribe(topicColour.c_str());
    mqtt.subscribe(topicBrightness.c_str());
    mqtt.subscribe(topicButton.c_str());
    statusPending = true;   // report the current state straight away
    lastStatusAt = 0;
    retryDelay = MQTT_RETRY_MS;
    return;
  }

  Serial.printf("failed (state %d)\n", mqtt.state());
#if MQTT_USE_TLS
  char reason[80];
  int sslError = mqttSocket.getLastSSLError(reason, sizeof(reason));
  if (sslError != 0) {
    Serial.printf("  TLS error %d: %s\n", sslError, reason);
    Serial.println("  (a certificate error usually means Adafruit IO changed certificate authority - see mqtt_roots.h)");
  }
#endif
  if (retryDelay == 0)          retryDelay = MQTT_RETRY_MS;
  else if (retryDelay < 30000)  retryDelay *= 2;
  else                          retryDelay = 60000;
  Serial.printf("  retrying in %lu s\n", (unsigned long)(retryDelay / 1000));
}

// Feeds:
//   espweb.brightness  0-100
//   espweb.color       colour name ("Blue", "Hot Pink") or hex ("#FF8800")
//   espweb.onoff and espweb.button
//                      ON, OFF, WHITE,
//                      SOLID, FADE, RAINBOW, RANDOM, SMOOTH (or PALETTE), STEPPED, CANDLE,
//                      or a palette name: CLOUD, OCEAN, PARTY, HEAT, FOREST, LAVA
//   espweb.outtopic    (published by the lamp) status report
void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  String feed(topic);
  String text;
  text.reserve(length);
  for (unsigned int i = 0; i < length; i++) text += (char)payload[i];
  text.trim();
  Serial.printf("Adafruit IO: %s = %s\n", feed.c_str(), text.c_str());

  if (feed == topicBrightness) {
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

// The report shown in the dashboard's status block.
String statusText() {
  String s;
  s.reserve(260);
  s += "Power: ";       s += lamp.on ? "On" : "Off";
  s += "\nMode: ";      s += MODE_NAMES[lamp.mode];
  if (lamp.mode == MODE_PALETTE_SMOOTH || lamp.mode == MODE_PALETTE_STEPPED) {
    s += "\nPalette: "; s += PALETTES[lamp.palette].name;
  }
  if (lamp.mode == MODE_SOLID) {
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", lamp.colour.r, lamp.colour.g, lamp.colour.b);
    s += "\nColour: ";  s += hex;
  }
  s += "\nBrightness: "; s += (lamp.brightness * 100 + 127) / 255; s += "%";
  s += "\nWi-Fi: ";     s += WiFi.SSID(); s += " ("; s += WiFi.RSSI(); s += " dBm)";
  s += "\nIP: ";        s += WiFi.localIP().toString();
  s += "\nHost: ";      s += DEVICE_NAME; s += ".local";
  s += "\nAdafruit IO: "; s += MQTT_USE_TLS ? "encrypted (TLS)" : "unencrypted";
  s += "\nFirmware: v"; s += FIRMWARE_VERSION; s += ", up "; s += uptimeText();
  return s;
}

String uptimeText() {
  uint32_t minutes = millis() / 60000;
  uint32_t days = minutes / 1440, hours = (minutes / 60) % 24, mins = minutes % 60;
  char text[24];
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
    lastStatusAt = (now == 0) ? 1 : now;
  }
}


// ===========================================================================
// Web interface
// ===========================================================================
// Endpoints (all GET, all answer with the lamp state as JSON):
//   /state
//   /power?on=1|0
//   /colour?hex=RRGGBB        (a colour name also works)
//   /brightness?value=1-255
//   /mode?id=1-7
//   /palette?id=0-5
void startWebServer() {
  web.on("/", []() { web.send_P(200, "text/html", WEB_PAGE); });

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
    long level = web.arg("value").toInt();
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

  web.onNotFound([]() { web.send(404, "text/plain", "Not found\n"); });
  web.begin();
}

void replyWithState() {
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", stateAsJson());
}

void replyBadRequest(const char *reason) {
  web.send(400, "text/plain", String(reason) + "\n");
}

String stateAsJson() {
  char colour[8];
  snprintf(colour, sizeof(colour), "#%02x%02x%02x", lamp.colour.r, lamp.colour.g, lamp.colour.b);

  String json;
  json.reserve(340);
  json += "{\"on\":";
  json += lamp.on ? "true" : "false";
  json += ",\"mode\":";       json += lamp.mode;
  json += ",\"palette\":";    json += lamp.palette;
  json += ",\"brightness\":"; json += lamp.brightness;
  json += ",\"colour\":\"";   json += colour;
  json += "\",\"palettes\":[";
  for (uint8_t i = 0; i < PALETTE_COUNT; i++) {
    if (i) json += ',';
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
  json += ",\"version\":\"";  json += FIRMWARE_VERSION;
  json += "\",\"uptime\":\""; json += uptimeText();
  json += "\"}";
  return json;
}
