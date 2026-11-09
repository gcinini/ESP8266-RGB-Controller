// =============================================================================
// config.h - the settings you change when you build or deploy a lamp
// =============================================================================
//
// Everything that differs between one lamp and another (its name, how many
// LEDs it has, which pin the strip is on...) lives in this file, so you never
// have to search through the main sketch to set up a lamp.
//
// Passwords and keys do NOT go here. They live in secrets.h, which git ignores.
// This file is committed to GitHub, so anything in it is public.
//
// -----------------------------------------------------------------------------
// A word about #define (for beginners)
// -----------------------------------------------------------------------------
// Lines starting with # are instructions to the *preprocessor*, a step that
// runs before the real compiler. `#define LED_COUNT 35` means "wherever the
// word LED_COUNT appears in the code, paste in 35". There is no variable in
// memory; the value is simply substituted into the code. That is why these
// settings can be used in places that need a fixed number at compile time,
// such as the size of an array (`CRGB leds[LED_COUNT];`).
//
//   #if / #elif / #else / #endif   choose which lines the compiler sees
//   #ifndef X ... #endif           "only if X has not been defined already"
//   #error "message"               stop the build with a message
// =============================================================================

#pragma once   // makes sure this file is only included once per build


// =============================================================================
// 1. Which lamp is this build for?
// =============================================================================
// Arduino IDE: change the number below before uploading to a different lamp.
// PlatformIO:  each lamp has its own environment in platformio.ini that sets
//              LAMP_ID for you (with -D LAMP_ID=2), so leave this at 1.
//
// The #ifndef lets PlatformIO's setting win: if LAMP_ID was already defined
// on the compiler command line, this line is skipped.
#ifndef LAMP_ID
#define LAMP_ID 1
#endif


// =============================================================================
// 2. Settings for each lamp
// =============================================================================
// Copy a whole block to add another lamp, give it the next number, and change
// its values. Only the block whose number matches LAMP_ID is used.
//
//   DEVICE_NAME        Network name (hostname), used for OTA and mDNS
//                      (<name>.local). Must be different on every lamp.
//                      Letters, digits and hyphens only.
//   LAMP_NAME          The friendly name people see and say: the device name
//                      in Home Assistant, the name Alexa knows the lamp by
//                      ("Alexa, turn on the bedlight") and the web page title.
//                      Give each lamp a different name. If you change it, ask
//                      Alexa to discover devices again and delete the old one
//                      in the Alexa app. (Home Assistant updates by itself.)
//   MQTT_TOPIC         Start of this lamp's MQTT topics: the lamp listens on
//                      <topic>/set and reports on <topic>/state. Lower case,
//                      no spaces, different on every lamp.
//   LED_COUNT          Number of LEDs on the strip.
//   LED_PIN            Board pin the strip's data wire is connected to.
//                      On a Wemos D1 Mini, D4 is GPIO2.
//   LED_TYPE           LED chip. WS2812B is the common "NeoPixel" type.
//   LED_ORDER          Order the chip expects its colours in. If red and green
//                      come out swapped, change GRB to RGB.
//   CANDLE_LEDS        How many LEDs, counted from the bottom, form the candle
//                      flame in Candle mode.
//   CANDLE_AT_LED_ZERO true if the first LED of the strip is at the bottom of
//                      the lamp. If the flame appears at the top, set false.

#if LAMP_ID == 1
  // --- Lamp 1: the bed headboard light bar (was a glass-jar lightbox) (34 LEDs) ---
  #define DEVICE_NAME          "ESP-bedlight"
  #define LAMP_NAME            "Bedlight"
  #define MQTT_TOPIC           "bedlight"
  #define LED_COUNT            34
  #define LED_PIN              D4
  #define LED_TYPE             WS2812B
  #define LED_ORDER            GRB
  #define CANDLE_LEDS          14
  #define CANDLE_AT_LED_ZERO   true

#elif LAMP_ID == 2
  // --- Lamp 2: example values, edit them for your second lamp ---
  #define DEVICE_NAME          "ESP-lightbox2"
  #define LAMP_NAME            "Lightbox 2"
  #define MQTT_TOPIC           "lightbox2"
  #define LED_COUNT            60
  #define LED_PIN              D4
  #define LED_TYPE             WS2812B
  #define LED_ORDER            GRB
  #define CANDLE_LEDS          20
  #define CANDLE_AT_LED_ZERO   true

#else
  #error "Unknown LAMP_ID: add a settings block for it in config.h"
#endif


// =============================================================================
// 3. Settings shared by all lamps
// =============================================================================
// You rarely need to change these. They are here so they are easy to find.

// --- Power -------------------------------------------------------------------
// Each WS2812B LED draws up to about 60 mA at full white, so 35 LEDs can draw
// over 2 A - more than a computer's USB port supplies. Set this to the current
// your power supply can safely deliver (for example 1500 for a 2 A supply) and
// FastLED will dim the whole strip just enough to stay under it.
// 0 means no limit.
#define MAX_MILLIAMPS          2000   // 2 A - my PSU is rated for 2.5 A, but I don't want to push it

// --- Starting state (used until the lamp has saved its own settings) -----------
#define DEFAULT_RED            255   // warm white
#define DEFAULT_GREEN          180
#define DEFAULT_BLUE           107
#define DEFAULT_BRIGHTNESS     128   // 1-255, so about half

// --- Animation ----------------------------------------------------------------
#define FRAME_INTERVAL_MS      20     // time between animation frames: 20 ms = 50 frames a second
#define RANDOM_HOLD_MS         8000   // how long Random mode keeps each palette
#define COLOUR_GAMMA           2.6f   // corrects picked colours for how LEDs look (see setColour)
#define DEFAULT_SPEED          5      // effect speed 1 (slow) to 10 (fast) until changed; 5 = the v4.0 speed

// --- Fades ----------------------------------------------------------------------
// Every change (on/off, brightness, colour) fades instead of jumping.
// Home Assistant can ask for a longer fade with "transition" (in seconds).
#define DEFAULT_FADE_MS        400    // fade length when nothing else is asked for
#define MAX_TRANSITION_S       3600   // longest fade accepted from Home Assistant (1 hour)

// --- Special effects ------------------------------------------------------------
#define SUNRISE_MINUTES        20     // Sunrise: dark red -> warm white over this long, unless
                                      // Home Assistant sends a "transition" with the effect
#define SLEEP_FADE_SECONDS     60     // when the sleep timer runs out, fade off over this long
#define SLEEP_MAX_MINUTES      120    // longest sleep timer

// Night light: a very dim amber that ignores the brightness setting.
#define NIGHT_LIGHT_LEVEL      12     // overall brightness 1-255 (12 is about 5%)
#define NIGHT_LIGHT_RED        255
#define NIGHT_LIGHT_GREEN      60
#define NIGHT_LIGHT_BLUE       0

// Reading light: bright in the middle, dim at the ends, in this white.
#define READING_LIGHT_KELVIN   3500

// --- White temperature ------------------------------------------------------------
// Range offered to Home Assistant's colour-temperature picker, in kelvin:
// 2000 K is candle-warm, 2700 K a warm bulb, 4000 K neutral, 6500 K daylight.
#define MIN_KELVIN             2000
#define MAX_KELVIN             6500

// --- Saving settings to flash --------------------------------------------------
#define SAVE_DELAY_MS          2000   // save this long after the last change

// --- Alexa ---------------------------------------------------------------------
// The lamp pretends to be a Philips Hue colour bulb, so an Amazon Echo on the
// same network finds it with "Alexa, discover devices" and controls it
// directly - no Alexa skill, no cloud service in between. Alexa can switch it
// on and off, set the brightness and set a colour (which selects Solid colour
// mode). Modes such as Candle are not available by voice.
// Set to 0 to turn this off. (It can also be set from secrets.h or a
// PlatformIO build flag, -D ENABLE_ALEXA=0, which wins.)
#ifndef ENABLE_ALEXA
#define ENABLE_ALEXA           1
#endif

// --- Home Assistant (MQTT) -----------------------------------------------------
// The lamp connects to an MQTT broker (Mosquitto) on your own network and
// announces itself to Home Assistant there ("MQTT discovery"), so it appears
// in Home Assistant by itself - no YAML to write. Login details for the
// broker go in secrets.h.
//
// Use the broker's IP address: a .local name doesn't work across VLANs.
#define MQTT_HOST              "192.168.10.61"   // Pi5, which runs Home Assistant and Mosquitto
#define MQTT_PORT              1883              // standard (unencrypted) MQTT port
#define MQTT_RETRY_MS          5000              // first wait before reconnecting; doubles up to 60 s

// Home Assistant listens for new devices under this prefix. "homeassistant"
// is Home Assistant's default - only change it if you changed it there too.
#define HA_DISCOVERY_PREFIX    "homeassistant"

// State reports to <topic>/state, which Home Assistant shows.
#define STATE_SETTLE_MS        250      // wait this long after a change, so a burst becomes one report
#define STATE_HEARTBEAT_MS     60000    // report every minute even if nothing changed (keeps Wi-Fi signal current)
