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
//   DEVICE_NAME        Network name. Shown on the web page and in Adafruit IO,
//                      and used for OTA. Must be different on every lamp.
//                      Letters, digits and hyphens only.
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
//   ALEXA_NAME         The name Alexa knows this lamp by ("Alexa, turn on the
//                      lightbox"). Give each lamp a different name. If you change
//                      it, ask Alexa to discover devices again and delete the old
//                      one in the Alexa app.
//   FEED_GROUP         Adafruit IO feed group. The lamp listens to
//                      <group>.onoff, .button, .color, .brightness and reports
//                      to <group>.outtopic. Give each lamp its own group so
//                      buttons and status reports don't mix. (Two lamps that
//                      share a group react to the same buttons, but overwrite
//                      each other's status report.)

#if LAMP_ID == 1
  // --- Lamp 1: the glass-jar lightbox (35 LEDs) ---
  #define DEVICE_NAME          "ESP-lightbox1"
  #define LED_COUNT            35
  #define LED_PIN              D4
  #define LED_TYPE             WS2812B
  #define LED_ORDER            GRB
  #define CANDLE_LEDS          14
  #define CANDLE_AT_LED_ZERO   true
  #define ALEXA_NAME           "Lightbox"
  #define FEED_GROUP           "espweb"

#elif LAMP_ID == 2
  // --- Lamp 2: example values, edit them for your second lamp ---
  #define DEVICE_NAME          "ESP-lightbox2"
  #define LED_COUNT            60
  #define LED_PIN              D4
  #define LED_TYPE             WS2812B
  #define LED_ORDER            GRB
  #define CANDLE_LEDS          20
  #define CANDLE_AT_LED_ZERO   true
  #define ALEXA_NAME           "Lightbox 2"
  #define FEED_GROUP           "lamp2"

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

// --- Saving settings to flash --------------------------------------------------
#define SAVE_DELAY_MS          2000   // save this long after the last change

// --- Alexa ---------------------------------------------------------------------
// The lamp pretends to be a Philips Hue colour bulb, so an Amazon Echo on the
// same network finds it with "Alexa, discover devices" and controls it
// directly - no Alexa skill, no cloud service in between. Alexa can switch it
// on and off, set the brightness and set a colour (which selects Solid colour
// mode). Modes such as Candle are not available by voice.
// Set to 0 to turn this off. (Like MQTT_USE_TLS below, it can also be set
// from secrets.h or a PlatformIO build flag, -D ENABLE_ALEXA=0, which wins.)
#ifndef ENABLE_ALEXA
#define ENABLE_ALEXA           1
#endif

// --- Adafruit IO (MQTT) ------------------------------------------------------
#define MQTT_HOST              "io.adafruit.com"
#define MQTT_RETRY_MS          5000   // first wait before reconnecting; doubles up to 60 s

// Encrypted connection (TLS on port 8883) is on by default. To turn it off,
// put `#define MQTT_USE_TLS 0` in secrets.h - only as a last resort, because
// your Adafruit IO key is then sent unencrypted.
#ifndef MQTT_USE_TLS
#define MQTT_USE_TLS           1
#endif

// Status reports to the <group>.outtopic feed. The free Adafruit IO plan
// allows 30 messages a minute, so reports are grouped and spaced out.
#define STATUS_SETTLE_MS       1500     // wait for changes to settle before reporting
#define STATUS_MIN_GAP_MS      5000     // never report more often than this
#define STATUS_HEARTBEAT_MS    600000   // report every 10 minutes even if nothing changed

// --- Clock ---------------------------------------------------------------------
// Certificates have expiry dates, so the lamp sets its clock from the internet
// before making the encrypted connection.
#define NTP_SERVER_1           "pool.ntp.org"
#define NTP_SERVER_2           "time.google.com"
