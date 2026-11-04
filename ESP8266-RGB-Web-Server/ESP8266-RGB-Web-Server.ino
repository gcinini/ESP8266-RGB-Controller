/*	ESP8266WiFiWebServer v 3.0.4
    ESP8266 RGB Web Server

Original souce was created by giznoise and is available at
		https://github.com/Makerspaceteam04/Arduino-ESP8266-RGB-Web-Server
		Original project found here https://github.com/giznoise/Arduino-ESP8266-RGB-Web-Server
*/
// Web server and Adafruit IO (MQTT) controller for a WS2812B LED strip on an ESP8266.
//
// How the code is organised (v3.0.4):
//   * Web handlers and MQTT messages only CHANGE STATE (on/off, mode, palette, colour,
//     brightness), save it to EEPROM and return immediately.
//   * loop() services Wi-Fi/OTA/web/MQTT and then draws ONE animation frame from the
//     current state (renderFrame). Nothing ever loops forever inside a handler, so web
//     clicks and MQTT messages are processed promptly and the stack never grows.
//
// Libraries: FastLED, PubSubClient, plus the ESP8266 core (WiFi, WebServer, mDNS,
// EEPROM, ArduinoOTA, LittleFS). The colour picker script (data/jscolor.js) is
// served from LittleFS, so upload the data/ folder as well as the sketch.


// Includes and Setup
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

#define FASTLED_ESP8266_RAW_PIN_ORDER //Refer to FastLED notes on ESP8266 for an explanation on these modes
//This define must be called before including FastLED
#include <FastLED.h>

#include "html_pages.h"

// Wi-Fi and Adafruit IO credentials live in secrets.h, which git ignores.
// Copy secrets.example.h to secrets.h and fill in your own values.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "secrets.h not found: copy secrets.example.h to secrets.h and fill in your credentials"
#endif
#include <LittleFS.h>

const char *HOSTNAME = "lightbox1";   // OTA / mDNS name

//FastLED setup
#define DATA_PIN  D4 // Using WeMos D4 pin for the control line
#define NUM_LEDS  35 // Make sure you change this to the number of LEDs in the strip
#define LED_TYPE  WS2812B
#define COLOR_ORDER GRB
#define UPDATES_PER_SECOND 50
CRGB leds[NUM_LEDS];
CRGBPalette16 currentPalette;
TBlendType    currentBlending;

extern const TProgmemPalette16 myRedWhiteBluePalette_p PROGMEM;
//End FastLED defines

//MQTT Connection Setup
#define mqtt_server "io.adafruit.com"
#define mqtt_user MQTT_USER
#define mqtt_password MQTT_KEY
#define MQTT_RETRY_MS 5000   // wait between reconnect attempts (non-blocking)

WiFiClient espClient;			// Declares a WiFiClient Object using ESP8266WiFi
PubSubClient client(espClient); // instanciates Pubsub MQTT client

#define control_topic "gcinini/feeds/espweb.onoff"
#define color_topic "gcinini/feeds/espweb.color"
#define brightness_topic "gcinini/feeds/espweb.brightness"
#define mqttclient_topic "gcinini/feeds/espweb.mqttclient"
#define button_topic "gcinini/feeds/espweb.button"
#define hello_topic "gcinini/feeds/espweb.outtopic"

//Wifi settings
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

ESP8266WebServer server ( 80 );


// ---------------------------------------------------------------------------
// Lamp state
// ---------------------------------------------------------------------------
// Modes - the numbers match the /set_modeN URLs and the values stored in EEPROM.
const uint8_t MODE_FIXED           = 1;  // one solid colour
const uint8_t MODE_FADE            = 2;  // whole lamp fades slowly through the Party palette
const uint8_t MODE_RAINBOW         = 3;  // rainbow scrolling along the strip
const uint8_t MODE_RANDOM          = 4;  // cycles through a set of palettes every few seconds
const uint8_t MODE_PALETTE_BLEND   = 5;  // chosen palette, smooth blending
const uint8_t MODE_PALETTE_NOBLEND = 6;  // chosen palette, hard steps

// Palettes selectable from the "Select Palette" page.
const uint8_t PAL_CLOUD = 0;
const uint8_t PAL_OCEAN = 1;
const uint8_t PAL_PARTY = 2;
const uint8_t PAL_HEAT  = 3;
const uint8_t PAL_COUNT = 4;

bool    lamp_on    = true;
uint8_t mode_flag  = MODE_FIXED;
uint8_t palette_id = PAL_CLOUD;
uint8_t red_int    = 0;     // fixed colour, already gamma adjusted
uint8_t green_int  = 0;
uint8_t blue_int   = 255;
uint8_t brightness = 255;   // applied with FastLED.setBrightness() only

//EEPROM Memory//
//Mem Location ||--0--||--1--||--2--||---3----||--4--||---5---||
//                red   green  blue   bright   mode   palette
const int EEPROM_SIZE   = 6;
const int ADDR_RED      = 0;
const int ADDR_GREEN    = 1;
const int ADDR_BLUE     = 2;
const int ADDR_BRIGHT   = 3;
const int ADDR_MODE     = 4;
const int ADDR_PALETTE  = 5;


// Function declarations with brief purpose notes.
// State changes (shared by the web interface and MQTT)
void loadState();                          // Restore colour, brightness, mode and palette from EEPROM.
void saveState();                          // Persist the current state to EEPROM.
void switchOn();                           // Turn the lamp on in the saved mode.
void switchOff();                          // Turn the lamp off (state is kept for the next "on").
void setMode(uint8_t mode);                // Change mode and turn the lamp on.
void selectPalette(uint8_t id);            // Choose a palette and show it.
void setColour(uint8_t r, uint8_t g, uint8_t b); // Fixed colour (raw 0-255 values, gamma is applied here).
void setBrightnessLevel(uint8_t level);    // Global brightness 0-255.
void applyPaletteForMode();                // Load the palette/blending the current mode needs.
bool parseColour(String value, uint8_t &r, uint8_t &g, uint8_t &b); // Colour name or hex -> RGB.

// Rendering
void renderFrame();                        // Draw one animation frame (called from loop()).
void fillLEDsFromPaletteColors(uint8_t colorIndex); // Apply palette colours across the LED strip.
void ChangePalettePeriodically();          // Advance the palette over time (Random mode).
void SetupTotallyRandomPalette();          // Fill the current palette with random colours.
void SetupBlackAndWhiteStripedPalette();   // Create a black-and-white striped palette.
void setupPurpleAndGreenPalette();         // Create a purple/green striped palette.

// Network
void setupWiFi();                          // Connect to the configured WiFi network.
void setupMDNS();                          // Start the mDNS responder for local discovery.
void setupOTA();                           // Enable Over-The-Air firmware updates.
void maintainMQTT();                       // Keep the MQTT connection alive without blocking.
void callback(char* topic, byte* payload, unsigned int length); // Process incoming MQTT messages.

// Web handlers
void redirectTo(const String &url);        // Send the browser back to a page.
void handleIndex();                        // Serve the main web UI page.
void handleNotFound();                     // Serve files from LittleFS or return 404.
void handleSwitchOn();                     // /switch_on
void handleSwitchOff();                    // /switch_off
void handleSetColour();                    // Serve the colour picker page.
void handleColour();                       // Receive a hex colour from the colour picker.
void handleSetBrightness();                // Serve the brightness control page.
void handleBrightness();                   // Receive a brightness value from the slider.
void handleSelectMode();                   // Serve the mode selection page.
void handleSelectPalette();                // Serve the palette selection page.
void handle_mode1();                       // Fixed-colour mode.
void handle_mode2();                       // Colour fade mode.
void handle_mode3();                       // Rainbow mode.
void handle_mode4();                       // Random palette cycling mode.
void handle_mode5();                       // Chosen palette with blending.
void handle_mode6();                       // Chosen palette without blending.
void handle_cloud();                       // Cloud palette.
void handle_party();                       // Party palette.
void handle_heat();                        // Heat palette.
void handle_ocean();                       // Ocean palette.
String getContentType(String filename);    // convert the file extension to the MIME type
bool handleFileRead(String path);          // send the right file to the client (if it exists)

// Custom palette used for red/white/blue themed animations.
// The 16 entries form a repeating pattern of red, gray, blue, and black,
// creating a striped color cycle that is easier on the eyes than pure white.
//
// Storing this palette in PROGMEM keeps the data in flash memory instead of RAM,
// which is important on an ESP8266 where RAM is limited. FastLED can read it
// from flash when the animation is rendered.
const TProgmemPalette16 myRedWhiteBluePalette_p PROGMEM =
{
  CRGB::Red,
  CRGB::Gray, // Gray is used instead of white to avoid an overly bright look
  CRGB::Blue,
  CRGB::Black,

  CRGB::Red,
  CRGB::Gray,
  CRGB::Blue,
  CRGB::Black,

  CRGB::Red,
  CRGB::Red,
  CRGB::Gray,
  CRGB::Gray,
  CRGB::Blue,
  CRGB::Blue,
  CRGB::Black,
  CRGB::Black
};


// ---------------------------------------------------------------------------
// SETUP and LOOP
// ---------------------------------------------------------------------------
void setup(void) {
  Serial.begin ( 115200 );

  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setDither(1); //Allows for low brightness not to flicker
  //FastLED.setMaxPowerInVoltsAndMilliamps(5, 900);

  // Restore the last state first so the lamp lights up while Wi-Fi connects
  EEPROM.begin(EEPROM_SIZE);
  loadState();
  renderFrame();

  setupWiFi(); 	// setup WiFi connection
  // setupMDNS (); // ArduinoOTA already advertises lightbox1.local
  setupOTA ();  // setup Arduino Over the air updating

  // MQTT Setup - subscriptions happen in maintainMQTT() once connected
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // Webpage handling setup
  server.on ( "/", handleIndex );
  server.onNotFound ( handleNotFound );
  server.on ( "/switch_on", handleSwitchOn);
  server.on ( "/switch_off", handleSwitchOff);
  server.on ( "/set_colour", handleSetColour);
  server.on ( "/set_colour_hash", handleColour );
  server.on ( "/set_brightness", handleSetBrightness);
  server.on ( "/set_bright_val", handleBrightness);
  server.on ( "/select_mode", handleSelectMode);
  server.on ( "/select_palette", handleSelectPalette);
  server.on ( "/set_mode1", handle_mode1);
  server.on ( "/set_mode2", handle_mode2);
  server.on ( "/set_mode3", handle_mode3);
  server.on ( "/set_mode4", handle_mode4);
  server.on ( "/set_mode5", handle_mode5);
  server.on ( "/set_mode6", handle_mode6);
  server.on ( "/set_cloud", handle_cloud);
  server.on ( "/set_party", handle_party);
  server.on ( "/set_heat",  handle_heat);
  server.on ( "/set_ocean", handle_ocean);

  LittleFS.begin(); // Initialize LittleFS
  server.begin(); // Instantiate HTTP server handler
  Serial.printf("Ready. Mode %u, palette %u, brightness %u\n", mode_flag, palette_id, brightness);
}


void loop ( void ) {
  ArduinoOTA.handle();     // Services OverTheAir Update requests
  server.handleClient();   // Web interface
  maintainMQTT();          // Adafruit IO (reconnects in the background if needed)
  renderFrame();           // Draw the next animation frame (rate limited)
}


// ---------------------------------------------------------------------------
// State changes - used by both the web interface and MQTT
// ---------------------------------------------------------------------------
void loadState() {
  red_int    = EEPROM.read(ADDR_RED);
  green_int  = EEPROM.read(ADDR_GREEN);
  blue_int   = EEPROM.read(ADDR_BLUE);
  brightness = EEPROM.read(ADDR_BRIGHT);
  mode_flag  = EEPROM.read(ADDR_MODE);
  palette_id = EEPROM.read(ADDR_PALETTE);

  // Fall back to safe values if the EEPROM is blank or corrupted
  if (mode_flag < MODE_FIXED || mode_flag > MODE_PALETTE_NOBLEND) mode_flag = MODE_FIXED;
  if (palette_id >= PAL_COUNT) palette_id = PAL_CLOUD;

  lamp_on = true;
  FastLED.setBrightness(brightness);
  applyPaletteForMode();
}

void saveState() {
  // EEPROM.write() only marks the buffer dirty when a value changes,
  // so commit() does not wear the flash when nothing changed.
  EEPROM.write(ADDR_RED, red_int);
  EEPROM.write(ADDR_GREEN, green_int);
  EEPROM.write(ADDR_BLUE, blue_int);
  EEPROM.write(ADDR_BRIGHT, brightness);
  EEPROM.write(ADDR_MODE, mode_flag);
  EEPROM.write(ADDR_PALETTE, palette_id);
  EEPROM.commit();
}

void switchOn() {
  lamp_on = true;
  applyPaletteForMode();
  Serial.printf("Switch on - mode %u\n", mode_flag);
}

void switchOff() {
  lamp_on = false;
  Serial.println("Switch off");
}

void setMode(uint8_t mode) {
  if (mode < MODE_FIXED || mode > MODE_PALETTE_NOBLEND) return;
  mode_flag = mode;
  lamp_on = true;
  applyPaletteForMode();
  saveState();
  Serial.printf("Mode %u selected\n", mode_flag);
}

void selectPalette(uint8_t id) {
  if (id >= PAL_COUNT) return;
  palette_id = id;
  // Keep "No Blending" if that is what is showing, otherwise use blending
  setMode(mode_flag == MODE_PALETTE_NOBLEND ? MODE_PALETTE_NOBLEND : MODE_PALETTE_BLEND);
  Serial.printf("Palette %u selected\n", palette_id);
}

void setColour(uint8_t r, uint8_t g, uint8_t b) {
  red_int   = gamma_adjust[r];
  green_int = gamma_adjust[g];
  blue_int  = gamma_adjust[b];
  setMode(MODE_FIXED);   // shows the colour, turns the lamp on and saves everything
  Serial.printf("Colour set to %u,%u,%u (after gamma)\n", red_int, green_int, blue_int);
}

void setBrightnessLevel(uint8_t level) {
  brightness = level;
  FastLED.setBrightness(brightness);
  saveState();
  Serial.printf("Brightness %u\n", brightness);
}

// Each mode needs a particular palette and blending; Random mode changes them itself.
void applyPaletteForMode() {
  switch (mode_flag) {
    case MODE_FADE:
      currentPalette = PartyColors_p;
      currentBlending = LINEARBLEND;
      break;
    case MODE_RAINBOW:
      currentPalette = RainbowColors_p;
      currentBlending = LINEARBLEND;
      break;
    case MODE_RANDOM:
      currentPalette = RainbowColors_p;   // first palette until ChangePalettePeriodically() takes over
      currentBlending = LINEARBLEND;
      break;
    case MODE_PALETTE_BLEND:
    case MODE_PALETTE_NOBLEND:
      switch (palette_id) {
        case PAL_OCEAN: currentPalette = OceanColors_p; break;
        case PAL_PARTY: currentPalette = PartyColors_p; break;
        case PAL_HEAT:  currentPalette = HeatColors_p;  break;
        default:        currentPalette = CloudColors_p; break;
      }
      currentBlending = (mode_flag == MODE_PALETTE_BLEND) ? LINEARBLEND : NOBLEND;
      break;
    default:
      break;   // fixed colour does not use a palette
  }
}

// Accepts a colour name from the list in html_pages.h (e.g. "Blue", "hot pink"),
// "White", or a hex value such as "#FF8800" (what the Adafruit IO colour picker sends).
bool parseColour(String value, uint8_t &r, uint8_t &g, uint8_t &b) {
  value.trim();
  if (value.equalsIgnoreCase("white")) {
    r = g = b = 255;
    return true;
  }
  for (int x = 0; x < 26; x++) {
    if (value.equalsIgnoreCase(colorType[x])) {
      r = colorNum[x][0];
      g = colorNum[x][1];
      b = colorNum[x][2];
      return true;
    }
  }
  if (value.startsWith("#")) value.remove(0, 1);
  if (value.length() != 6) return false;
  for (unsigned int i = 0; i < 6; i++) {
    if (!isxdigit(value[i])) return false;
  }
  uint32_t rgb = strtoul(value.c_str(), NULL, 16);
  r = (rgb >> 16) & 0xFF;
  g = (rgb >> 8) & 0xFF;
  b = rgb & 0xFF;
  return true;
}


// ---------------------------------------------------------------------------
// Rendering - one frame per call, at most UPDATES_PER_SECOND times a second
// ---------------------------------------------------------------------------
void renderFrame() {
  static uint32_t lastFrame = 0;
  static uint8_t startIndex = 0;   // animation position
  static uint8_t frameCount = 0;

  uint32_t now = millis();
  if (now - lastFrame < 1000 / UPDATES_PER_SECOND) return;
  lastFrame = now;
  frameCount++;

  if (!lamp_on) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    return;
  }

  switch (mode_flag) {
    case MODE_FIXED:
      fill_solid(leds, NUM_LEDS, CRGB(red_int, green_int, blue_int));
      break;

    case MODE_FADE:
      // Whole lamp is one colour, drifting slowly through the palette (about 20 s per cycle)
      if (frameCount % 4 == 0) startIndex++;
      fill_solid(leds, NUM_LEDS, ColorFromPalette(currentPalette, startIndex, 255, currentBlending));
      break;

    case MODE_RANDOM:
      ChangePalettePeriodically();
      startIndex++;
      fillLEDsFromPaletteColors(startIndex);
      break;

    case MODE_RAINBOW:
    case MODE_PALETTE_BLEND:
    case MODE_PALETTE_NOBLEND:
    default:
      startIndex++;
      fillLEDsFromPaletteColors(startIndex);
      break;
  }
  FastLED.show();
}

void fillLEDsFromPaletteColors( uint8_t colorIndex) {
  for ( int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette( currentPalette, colorIndex, 255, currentBlending);  // brightness comes from FastLED.setBrightness()
    colorIndex += 3;
  }
}

void ChangePalettePeriodically() {
  uint8_t secondHand = (millis() / 1000) % 60;
  static uint8_t lastSecond = 99;
  if ( lastSecond != secondHand) {
    lastSecond = secondHand;
    if ( secondHand ==  0)  {
      currentPalette = RainbowColors_p;
      currentBlending = LINEARBLEND;
    }
    if ( secondHand == 10) {
      currentPalette = RainbowStripeColors_p;
      currentBlending = NOBLEND;
    }
    if ( secondHand == 15) {
      currentPalette = RainbowStripeColors_p;
      currentBlending = LINEARBLEND;
    }
    if ( secondHand == 20) {
      setupPurpleAndGreenPalette();
      currentBlending = LINEARBLEND;
    }
    if ( secondHand == 25) {
      SetupTotallyRandomPalette();
      currentBlending = LINEARBLEND;
    }
    if ( secondHand == 30) {
      SetupBlackAndWhiteStripedPalette();
      currentBlending = NOBLEND;
    }
    if ( secondHand == 35) {
      SetupBlackAndWhiteStripedPalette();
      currentBlending = LINEARBLEND;
    }
    if ( secondHand == 40) {
      currentPalette = CloudColors_p;
      currentBlending = LINEARBLEND;
    }
    if ( secondHand == 45) {
      currentPalette = PartyColors_p;
      currentBlending = LINEARBLEND;
    }
    if ( secondHand == 50) {
      currentPalette = myRedWhiteBluePalette_p;
      currentBlending = NOBLEND;
    }
    if ( secondHand == 55) {
      currentPalette = myRedWhiteBluePalette_p;
      currentBlending = LINEARBLEND;
    }
  }
}

// This function fills the palette with totally random colors.
void SetupTotallyRandomPalette() {
  for ( int i = 0; i < 16; i++) {
    currentPalette[i] = CHSV( random8(), 255, random8());
  }
}

// This function sets up a palette of black and white stripes,
// using code.  Since the palette is effectively an array of
// sixteen CRGB colors, the various fill_* functions can be used
// to set them up.
void SetupBlackAndWhiteStripedPalette() {
  fill_solid( currentPalette, 16, CRGB::Black); // 'black out' all 16 palette entries...
  // and set every fourth one to white.
  currentPalette[0] = CRGB::White;
  currentPalette[4] = CRGB::White;
  currentPalette[8] = CRGB::White;
  currentPalette[12] = CRGB::White;
}

// This function sets up a palette of purple and green stripes.
void setupPurpleAndGreenPalette() {
  CRGB purple = CHSV( HUE_PURPLE, 255, 255);
  CRGB green  = CHSV( HUE_GREEN, 255, 255);
  CRGB black  = CRGB::Black;
  currentPalette = CRGBPalette16(
                     green,  green,  black,  black,
                     purple, purple, black,  black,
                     green,  green,  black,  black,
                     purple, purple, black,  black );
}


// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
void setupWiFi() {
	WiFi.mode(WIFI_STA);
	WiFi.hostname(HOSTNAME);
	WiFi.begin ( ssid, password );
	Serial.print ( "Connecting to WiFi " );
	Serial.print ( ssid );
	// Wait for connection
	while ( WiFi.status() != WL_CONNECTED ) {
		delay ( 500 );
		Serial.print ( "." );
	}
	Serial.print(" Success! Connected to SSID ");
	Serial.println(ssid);
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());
	Serial.println("");
}

//Setting up mDNS service (not needed while ArduinoOTA is running)
void setupMDNS () {
  if (MDNS.begin(HOSTNAME)) {
    Serial.println("mDNS responder started");
  } else {
    Serial.println("Error setting up MDNS responder!");
  }
}

// Over-the-Air Update support setup and startup
void setupOTA () {
	ArduinoOTA.setHostname(HOSTNAME);
	ArduinoOTA.onStart([]() {
		Serial.println("Over the air update - OTA - Started");
		});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nOTA Update Finished");
		});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
		});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
		else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
		else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
		else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
		else if (error == OTA_END_ERROR) Serial.println("End Failed");
		});
	ArduinoOTA.begin();
}

// Keeps the MQTT connection alive. If it drops, one reconnect attempt is made
// every MQTT_RETRY_MS while the lamp and web interface keep working.
void maintainMQTT() {
  if (client.connected()) {
    client.loop();   // process incoming messages and keep-alives
    return;
  }

  static uint32_t lastAttempt = 0;
  static bool firstAttempt = true;
  uint32_t now = millis();
  if (!firstAttempt && now - lastAttempt < MQTT_RETRY_MS) return;
  firstAttempt = false;
  lastAttempt = now;

  if (WiFi.status() != WL_CONNECTED) return;

  String clientId = "ESP8266Client-";
  clientId += String(random(0xffff), HEX);
  Serial.println("Attempting MQTT connection as " + clientId);
  if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
    Serial.println("MQTT connected");
    client.subscribe(control_topic);
    client.subscribe(color_topic);
    client.subscribe(brightness_topic);
    client.subscribe(button_topic);
    client.publish(hello_topic, "hello world");
  } else {
    Serial.print("MQTT failed, rc=");
    Serial.print(client.state());
    Serial.println(" - retrying in 5 seconds");
  }
}

//MQTT Callback function
//  espweb.brightness : 0-100
//  espweb.color      : colour name ("Blue", "Hot Pink", "White") or hex ("#FF8800")
//  other feeds       : ON, OFF, RAINBOW, RANDOM, WHITE
void callback(char* topic, byte* payload, unsigned int length) {
  // Copy the message out of the library's buffer before doing anything else
  String topicStr = String(topic);
  String value;
  value.reserve(length);
  for (unsigned int i = 0; i < length; i++) value += (char)payload[i];
  value.trim();
  Serial.println("MQTT message [" + topicStr + "]: " + value);

  if (topicStr == brightness_topic) {
    int level = constrain(value.toInt(), 0, 100);
    setBrightnessLevel(map(level, 0, 100, 0, 255));
    return;
  }

  if (topicStr == color_topic) {
    uint8_t r, g, b;
    if (parseColour(value, r, g, b)) {
      setColour(r, g, b);
    } else {
      Serial.println("Unknown colour: " + value);
    }
    return;
  }

  if (value.equalsIgnoreCase("on"))           switchOn();
  else if (value.equalsIgnoreCase("off"))     switchOff();
  else if (value.equalsIgnoreCase("rainbow")) setMode(MODE_RAINBOW);
  else if (value.equalsIgnoreCase("random"))  setMode(MODE_RANDOM);
  else if (value.equalsIgnoreCase("white"))   setColour(255, 255, 255);
  else Serial.println("Unknown command: " + value);
}


// ---------------------------------------------------------------------------
// Web interface - every handler changes state, then sends the browser back
// ---------------------------------------------------------------------------
void redirectTo(const String &url) {
  String html = "<SCRIPT language='JavaScript'>window.location='";
  html += url;
  html += "';</SCRIPT>";
  server.send ( 200, "text/html", html );
}

// Function that handles an HTTP get for the index page
void handleIndex() {
  Serial.println ( "Request for index page received" );
  server.send ( 200, "text/html", page_contents);
}

void handleNotFound() {
  if (!handleFileRead(server.uri())) {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for ( uint8_t i = 0; i < server.args(); i++ ) {
      message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
    }
    server.send ( 404, "text/plain", message );
  }
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".js")) return "application/javascript";
  return "text/plain";
}

// send the right file to the client (if it exists)
bool handleFileRead(String path) {
  Serial.println("handleFileRead: " + path);
  String contentType = getContentType(path);		// Get the MIME type
  if (LittleFS.exists(path)) {                        // If the file exists
    File file = LittleFS.open(path, "r");             // Open it
    server.streamFile(file, contentType);             // And send it to the client
    file.close();                                     // Then close the file again
    return true;
  }
  Serial.println("\tFile Not Found");
  return false; // If the file doesn't exist, return false
}

void handleSwitchOn()  { switchOn();  redirectTo("/"); }
void handleSwitchOff() { switchOff(); redirectTo("/"); }

void handleSetColour() {
  server.send ( 200, "text/html", colour_picker);
}

void handleSetBrightness() {
  server.send ( 200, "text/html", bright_set);
}

void handleSelectMode() {
  server.send ( 200, "text/html", mode_page );
}

void handleSelectPalette() {
  server.send ( 200, "text/html", palette_page );
}

// The colour picker sends the colour as RRGGBB (jscolor leaves out the #)
void handleColour() {
  String hex = server.arg(0);
  hex.replace("%23", "");
  hex.replace("#", "");
  uint8_t r, g, b;
  if (parseColour(hex, r, g, b)) {
    setColour(r, g, b);
  } else {
    Serial.println("Invalid colour from web page: " + hex);
  }
  redirectTo("/set_colour?" + hex);   // back to the picker, showing the chosen colour
}

void handleBrightness() {
  int level = constrain(server.arg(0).toInt(), 1, 255);   // slider range is 1-255
  setBrightnessLevel(level);
  redirectTo("/set_brightness?" + String(brightness));
}

void handle_mode1() { setMode(MODE_FIXED);           redirectTo("/"); }
void handle_mode2() { setMode(MODE_FADE);            redirectTo("/"); }
void handle_mode3() { setMode(MODE_RAINBOW);         redirectTo("/"); }
void handle_mode4() { setMode(MODE_RANDOM);          redirectTo("/"); }
void handle_mode5() { setMode(MODE_PALETTE_BLEND);   redirectTo("/"); }
void handle_mode6() { setMode(MODE_PALETTE_NOBLEND); redirectTo("/"); }

void handle_cloud() { selectPalette(PAL_CLOUD); redirectTo("/"); }
void handle_ocean() { selectPalette(PAL_OCEAN); redirectTo("/"); }
void handle_party() { selectPalette(PAL_PARTY); redirectTo("/"); }
void handle_heat()  { selectPalette(PAL_HEAT);  redirectTo("/"); }
