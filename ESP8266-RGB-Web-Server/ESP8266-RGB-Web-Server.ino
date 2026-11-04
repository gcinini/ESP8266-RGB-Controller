/*	ESP8266WiFiWebServer v 3.0.2
    ESP8266 RGB Web Server

	Previous version c 2.1
		Copyright (c) 2015, Majenko Technologies
		All rights reserved.
		https://github.com/Makerspaceteam04/Arduino-ESP8266-RGB-Web-Server
		Original project found here https://github.com/giznoise/Arduino-ESP8266-RGB-Web-Server
*/
// This sketch is a web server that controls an RGB LED strip using an ESP8266 microcontroller.
// It allows users to change the color and brightness of the LEDs through a web interface.
// The code also includes MQTT functionality for remote control and OTA updates for firmware upgrades.
// The sketch uses the FastLED library for LED control and LittleFS for file system management.
// The web server serves HTML pages and handles user input to control the LED strip.
// The code is designed to be run on an ESP8266 microcontroller and requires the ESP8266WiFi, FastLED, and other libraries.
// The code is structured to handle different modes of operation, including fixed color, rainbow, and random color modes.
// It also includes functions for handling color selection, brightness adjustment, and palette selection.
// The code is modular and includes functions for setting up the web server, handling MQTT messages, and managing OTA updates.
// The code is designed to be user-friendly and provides a simple interface for controlling the LED strip.


// Includes and Setup
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

#define HOSTNAME lightbox1
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

//FastLED setup
#define DATA_PIN  D4 // Using WeMos D4 pin for the control line
#define NUM_LEDS  30 // Make sure you change this to the number of LEDs in the strip
#define LED_TYPE  WS2812B
#define COLOR_ORDER GRB
#define UPDATES_PER_SECOND 50
static uint8_t startIndex = 0;
CRGB leds[NUM_LEDS];
CRGBPalette16 currentPalette;
TBlendType    currentBlending;

extern CRGBPalette16 myRedWhiteBluePalette;
extern const TProgmemPalette16 myRedWhiteBluePalette_p PROGMEM;
//End FastLED defines

//MQTT Connection Setup
#define mqtt_server "io.adafruit.com"
#define mqtt_user MQTT_USER
#define mqtt_password MQTT_KEY

WiFiClient espClient;			// Declares a WiFiClient Object using ESP8266WiFi
PubSubClient client(espClient); // instanciates Pubsub MQTT client

#define control_topic "gcinini/feeds/espweb.onoff"
#define color_topic "gcinini/feeds/espweb.color"
#define brightness_topic "gcinini/feeds/espweb.brightness"
#define mqttclient_topic "gcinini/feeds/espweb.mqttclient"
#define button_topic "gcinini/feeds/espweb.button"

//Wifi settings
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

//MDNSResponder mdns;

ESP8266WebServer server ( 80 );

const int delayval = 500;
String rgb_now = "#0000ff";    //global rgb state values for use in various html pages
long red_int = 0;
long green_int = 0;
long blue_int = 255;
int brightness = 255;
int mode_flag = 1; // Initial mode set to 1

uint32_t Wheel(byte);

String getContentType(String filename); // convert the file extension to the MIME type
bool handleFileRead(String path);       // send the right file to the client (if it exists)

// Function declarations with brief purpose notes.
void allWhiteFullBright();                 // Set all LEDs to full white.
void handleIndex();                        // Serve the main web UI page.
void handleNotFound();                     // Return a 404 for missing files/routes.
void handleSwitchOn();                     // Restore the saved mode and turn LEDs on.
void handleSwitchOff();                    // Turn the strip off and restore default state.
void handleSetColour();                    // Serve the colour picker page.
void handleColour();                       // Receive a hex colour from the web UI and update LEDs.
void handleSetBrightness();                // Serve the brightness control page.
void handleBrightness();                   // Receive a brightness value and apply it.
void handleSelectMode();                   // Serve the mode selection page.
void handleSelectPalette();                // Serve the palette selection page.
void handle_mode1();                       // Fixed-colour mode.
void handle_mode2();                       // Colour fade / party palette mode.
void handle_mode3();                       // Rainbow animation mode.
void handle_mode4();                       // Random palette cycling mode.
void handle_mode5();                       // Palette animation with linear blending.
void handle_mode6();                       // Palette animation with no blending.
void light_up_all();                       // Fill the strip with the current RGB colour.
void turn_off_all();                       // Turn all LEDs off.
void handle_cloud();                       // Cloud palette animation mode.
void handle_party();                       // Party palette animation mode.
void handle_heat();                        // Heat palette animation mode.
void handle_ocean();                       // Ocean palette animation mode.
void setupWiFi();                          // Connect to the configured WiFi network.
void setupMDNS();                          // Start the mDNS responder for local discovery.
void reconnectMQTT();                      // Reconnect to the MQTT broker if disconnected.
void callback(char* topic, byte* payload, unsigned int length); // Process incoming MQTT messages.
void setupOTA();                           // Enable Over-The-Air firmware updates.
void fillLEDsFromPaletteColors(uint8_t colorIndex); // Apply palette colors across the LED strip.
void ChangePalettePeriodically();           // Advance the active palette over time.
void SetupTotallyRandomPalette();          // Fill the current palette with random colours.
void SetupBlackAndWhiteStripedPalette();   // Create a black-and-white striped palette.
void setupPurpleAndGreenPalette();         // Create a purple/green striped palette.
void handleColour();                       // Alias for the hex colour handler.
void handleBrightness();                   // Alias for the brightness handler.
void light_up_all();                       // Alias for the fixed-colour renderer.
void turn_off_all();                       // Alias for the LED shutdown helper.

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

// SETUP
void setup(void) {
  Serial.begin ( 115200 );
  
  //WiFi.setSleepMode(WIFI_NONE_SLEEP);
  
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setDither(1); //Allows for low brightness not to flicker
  //FastLED.setMaxPowerInVoltsAndMilliamps(5, 900);

  setupWiFi(); 	// setup WiFi connection
  // setupMDNS (); // call setupMDNS function
  setupOTA ();  // setup Arduino Over the air updating
  EEPROM.begin(5); // set up EEPROM storage space for config values
  
  // MQTT Setup
  client.setServer(mqtt_server, 1883);
  client.subscribe(control_topic);
  client.subscribe(color_topic);
  client.subscribe(brightness_topic);
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
  
  //EEPROM Memory//
  //Mem Location ||--0--||--1--||--2--||--3--||--4--||--5--||--6--||
  //                red   green  blue   bright  mode
  //restore colour to last used value. Ensures RGB lamp is same colour as when last switched off
  red_int = EEPROM.read(0);       
  green_int = EEPROM.read(1);
  blue_int = EEPROM.read(2);
  brightness = EEPROM.read(3);
  mode_flag = EEPROM.read(4);
  
  LittleFS.begin(); // Initialize LittleFS
  server.begin(); // Instantiate HTTP server handler
  handleSwitchOn(); // Call the switch-on function to set the initial state of the LEDs
}


void loop ( void ) {
  //MQTT Handling - first check if MQTT connection is up and calls reconnect if not
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop(); // PubsubClient client process incoming messages and maintains MQTT connection to server
	Serial.print ( "Mode: ");
  Serial.println (mode_flag);
	//MDNS.update();
	
	ArduinoOTA.handle(); // Services OverTheAir Update requests
    
  server.handleClient();	// ESPP8266WebServer handles Web server input 
  
	// FastLED.setBrightness(brightness);
	//currentPalette = RainbowColors_p;
	//currentBlending = LINEARBLEND;
	//startIndex = 0;
	//ChangePalettePeriodically();
	//uint8_t startIndex = 0;
	//startIndex = startIndex + 1; /* motion speed */
	//fillLEDsFromPaletteColors( startIndex);
	//FastLED.show();
	//FastLED.delay(1000 / UPDATES_PER_SECOND);

	if (mode_flag == 1) {
		light_up_all(); //set mode to default state - all led's on, fixed colour. This loop will service any brightness changes
		FastLED.delay(1000 / UPDATES_PER_SECOND);
	}

	if (mode_flag == 2 || mode_flag == 3) {
		startIndex = 0;
		startIndex = startIndex + 1; /* motion speed */

		fillLEDsFromPaletteColors( startIndex);

		FastLED.show();
		FastLED.delay(1000 / UPDATES_PER_SECOND);
	}

	if (mode_flag == 4) {
		ChangePalettePeriodically();
		static uint8_t startIndex = 0;
		startIndex = startIndex + 1; /* motion speed */

		fillLEDsFromPaletteColors( startIndex);

		FastLED.show();
		FastLED.delay(1000 / UPDATES_PER_SECOND);
	}

	if (mode_flag == 5 || mode_flag == 6) {
		static uint8_t startIndex = 0;
		startIndex = startIndex + 1; /* motion speed */

		fillLEDsFromPaletteColors( startIndex);

		FastLED.show();
		FastLED.delay(1000 / UPDATES_PER_SECOND);
	}
}


void setupWiFi() {
	WiFi.mode(WIFI_STA);
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


//Setting up mDNS service
void setupMDNS () {
  delay(1000);
  if (!MDNS.begin("lightbox1")) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  }

  void reconnectMQTT() {
	while (!client.connected()) // Loop until we're reconnected
	{
		Serial.println("Attempting MQTT connection... ");
		// Create a random client ID
		String clientId = "ESP8266Client-";
		clientId += String(random(0xffff), HEX);
		Serial.println("MQTT Client ID: "+ clientId);
		// Attempt to connect
		if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
			Serial.println("connected");
			client.subscribe(control_topic);
			client.subscribe(color_topic);
			client.subscribe(brightness_topic);
			client.subscribe(button_topic);
			client.publish("gcinini/feeds/espweb.outtopic", "hello world");
		} else {
			Serial.print("failed, rc=");
			Serial.print(client.state());
			Serial.println(" try again in 5 seconds");
			delay(5000);
		}
	}
}

// Checks if new mode is already in EEPROM, if not updates EEPROM with received parameter
void eepromModeCheckAndUpdate(int mode) {
  mode_flag = EEPROM.read(4);
  if (mode == mode_flag) {
    return;
  }
  EEPROM.write(4, mode);
  EEPROM.commit();
  Serial.println("EEPROM mode updated");
}
 


//MQTT Callback function
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT message arrived [");
  Serial.print(topic);
  Serial.println("] ");
  Serial.print("payload: ");
  String RAINBOW = String("RAINBOW");
  String RANDOM = String("RANDOM");
  String WHITE = String("white");
  payload[length] = '\0';
  String value = String((char*)payload);
  Serial.println(value);
  if (value.equalsIgnoreCase("On")) {
    Serial.println("ON Received!");
    handleSwitchOn();
  }
  if (value.equalsIgnoreCase("Off")) {
    Serial.println("OFF Received!");
    handleSwitchOff();
  }
  if (value.equalsIgnoreCase(RAINBOW)) {
    Serial.println("RAINBOW Received!");
    handle_mode3();
  }
  if (value.equalsIgnoreCase(RANDOM)) {
    Serial.print("RANDOM Received!");
    handle_mode4();
  }
  if (value.equalsIgnoreCase(WHITE)) {
    Serial.print("White Received!");
    allWhiteFullBright();
  }
  if (strcmp(topic,brightness_topic)==0) {
    int brightness_holder = value.toInt();
    brightness = map(brightness_holder, 0, 100, 0, 255);
    FastLED.setBrightness(brightness);
    EEPROM.write(3, brightness);  //write the brightness value to EEPROM to be restored on start-up
    EEPROM.commit();
  }
  if (strcmp(topic,color_topic)==0) {
    int x = 0; 
    for (x=0; x<26; x=x+1) {
      //if (colorType[x] == value){
      //if (value.equalsIgnoreCase(colorType[x])) {
        Serial.print("Color received: ");
        Serial.println(value);
        Serial.print("Array value: ");
        Serial.println(colorType[x]);
        Serial.print("RGB vals: ");
        Serial.print(colorNum[x][0]);
        Serial.print(colorNum[x][1]);
        Serial.println(colorNum[x][2]);
        mode_flag = 1;
        red_int = colorNum[x][0];
        green_int = colorNum[x][1];
        blue_int = colorNum[x][2];
        red_int = gamma_adjust[red_int];
        green_int = gamma_adjust[green_int];
        blue_int = gamma_adjust[blue_int];
        //write the colour values to EEPROM to be restored on start-up
        EEPROM.write(0, red_int);   
        EEPROM.write(1, green_int);
        EEPROM.write(2, blue_int);
        EEPROM.write(4, mode_flag);
        EEPROM.commit();
        light_up_all();
        //}
      }
    }
  Serial.println (value);
  for (int i=0;i<length;i++) {
    Serial.print((char)payload[i]);
    }
  Serial.println();
}

// Over-the-Air Update support setup and startup
void setupOTA () {
	ArduinoOTA.setHostname("lightbox1");
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
  //if (path.endsWith("/")) path += "index.html";	// If a folder is requested, send the index file
  String contentType = getContentType(path);		// Get the MIME type
  if (LittleFS.exists(path)) {                        // If the file exists
    File file = LittleFS.open(path, "r");             // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                   // Then close the file again
    return true;
  }
  Serial.println("\tFile Not Found");
  return false; // If the file doesn't exist, return false
}


void handleSwitchOn() {
  mode_flag = EEPROM.read(4); // Read mode flag from EEPROM
  delay(100);
  switch (mode_flag) {
    case 1: handle_mode1();
    break;
    case 2: handle_mode2();
    break;
    case 3: handle_mode3();
    break;
    case 4: handle_mode4();
    break;
    case 5: handle_mode5();
    break;
    case 6: handle_mode6();
    break;
    case 7: handle_cloud();
    default:
    light_up_all(); //Default to fixed colour should the EEProm becomes corrupted
    break;
  }
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>" );
}

void handleSwitchOff() {
  mode_flag = 1; //go to default fixed color mode and turn off all pixels
  delay(100);
  turn_off_all();
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>" );
}

void handleSetColour() {
  server.send ( 200, "text/html", colour_picker);
}

void handleSetBrightness() {
  server.send ( 200, "text/html", bright_set);
}

void handleSelectMode() {
  server.send ( 200, "text/html", mode_page );
  //  Serial.println ( "Mode select page" );
}

void handleSelectPalette() {
  server.send ( 200, "text/html", palette_page );
  Serial.println ( "Palette select page" );
}


// 
void fillLEDsFromPaletteColors( uint8_t colorIndex) {
  //uint8_t brightness = 255;
  for ( int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette( currentPalette, colorIndex, brightness, currentBlending);
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
// to set them up. */
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


void handleColour() {
  char buf_red[3];  //char buffers to hold 'String' value converted to char array
  char buf_green[3];
  char buf_blue[3];
  String message = server.arg(0); //get the 1st argument from the url which is the hex rgb value from the colour picker ie. #rrggbb (actually %23rrggbb)
  rgb_now = message;
  rgb_now.replace("%23", "#");  // change %23 to # as we need this in one of html pages
  Serial.println("handleColour funtion - rgb_now value: "+rgb_now);
  //String red_val = rgb_now.substring(1, 3);      //extract the rgb values
  //String green_val = rgb_now.substring(3, 5);
  //String blue_val = rgb_now.substring(5, 7);
  String red_val = rgb_now.substring(0, 2);      //extract the rgb values
  String green_val = rgb_now.substring(2, 4);
  String blue_val = rgb_now.substring(4, 6);     //Offsetting array. JSCOLOR does not send the # with the data
  Serial.println(red_val);
  Serial.println(green_val);
  Serial.println(blue_val);
  mode_flag = 1;  //get to fixed colour mode if not already
  
  red_val.toCharArray(buf_red, 3);  //convert hex 'String'  to Char[] for use in strtol()
  green_val.toCharArray(buf_green, 3);
  blue_val.toCharArray(buf_blue, 3);
  red_int = gamma_adjust[strtol( buf_red, NULL, 16)]; //convert hex chars to ints and apply gamma adjust
  green_int = gamma_adjust[strtol( buf_green, NULL, 16)];
  blue_int = gamma_adjust[strtol( buf_blue, NULL, 16)];

  EEPROM.write(0, red_int); //write the colour values to EEPROM to be restored on start-up
  EEPROM.write(1, green_int);
  EEPROM.write(2, blue_int);
  //trying to fix on and off
  eepromModeCheckAndUpdate(mode_flag);
  //end test
  EEPROM.commit();

  light_up_all();
  String java_redirect = "<SCRIPT language='JavaScript'>window.location='/set_colour?";
  java_redirect += message;     //send hash colour value in URL to update the colour picker control
  java_redirect += "';</SCRIPT>";
  server.send ( 200, "text/html", java_redirect );  // all done! - take user back to the colour picking page
}

void handleBrightness() {
  String message = server.arg(0); //get the 1st argument from the url which is the brightness level set by the slider
  String bright_val = message.substring(0, 3);   //extract the brightness value from the end of the argument in the URL
  brightness =  bright_val.toInt();
  FastLED.setBrightness(brightness);
  EEPROM.write(3, brightness);  //write the brightness value to EEPROM to be restored on start-up
  EEPROM.commit();
  String java_redirect = "<SCRIPT language='JavaScript'>window.location='/set_brightness?";
  java_redirect += brightness;  //send brightness value in URL to update the slider control
  java_redirect += "';</SCRIPT>";
  server.send ( 200, "text/html", java_redirect); // all done! - take user back to the brightness selection page
}

void light_up_all() {
  fill_solid(leds, NUM_LEDS, CRGB(brightness * red_int / 255, brightness * green_int / 255, brightness * blue_int / 255));
  FastLED.show();
}

void turn_off_all() {
  mode_flag = 999;  //go to non-existent mode and turn off all pixels
  fill_solid(leds, NUM_LEDS, CRGB(0, 0, 0));
  FastLED.show();
}

void allWhiteFullBright(){
  mode_flag = 1;
  fill_solid(leds, NUM_LEDS, CRGB(255,255,255));
  FastLED.show();
}

//fixed colour mode
void handle_mode1() {
	mode_flag = 1;
	Serial.println("On Mode 1 - Fixed Color Mode");
	eepromModeCheckAndUpdate(mode_flag);
	server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>"); 
}

//colour fade mode
void handle_mode2() {
	mode_flag = 2;
	Serial.println("On Mode 2 - Color Fade");
	eepromModeCheckAndUpdate(mode_flag);
	server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
	uint16_t i, j, k;
	int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
	while (mode_flag == 2)
	{
	for (j = 0; j < 256; j++)
	{
	  loop();
	  for (i = 0; i < NUM_LEDS; i++)
	  {
		loop();
		//startIndex = 0;
		//startIndex = startIndex + 1;
		currentPalette = PartyColors_p;
		currentBlending = LINEARBLEND;
		//fillLEDsFromPaletteColors(startIndex);
		//FastLED.show();
	  } loop();
	  //startIndex = 0;
	  for (k = 0; k < 200; k++) // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
	  {
		delay(wait);
		loop();
	  } loop();
	}
	return;
	}
}

//rainbow mode
void handle_mode3() {
  mode_flag = 3;
  Serial.println("On Mode 3 - Rainbow");
  eepromModeCheckAndUpdate(mode_flag);
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
  uint16_t i, j, k;
  int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
  while (mode_flag == 3)  // do this indefenitely or until mode changes
  {
    for (j = 0; j < 256 * 5; j++) // 5 cycles of all colors on wheel
    {
      loop();
      for (i = 0; i < NUM_LEDS; i++)
      {
        loop();
        currentPalette = RainbowColors_p;
        currentBlending = LINEARBLEND;
        //fillLEDsFromPaletteColors(startIndex);
        //FastLED.show();
      }
      
      //FastLED.show();
      
      for (k = 0; k < 50; k++)  // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
      {
        delay(wait);
        loop();
      }
    }
  } return;
}

//Random mode
void handle_mode4() {
  mode_flag = 4;
  Serial.println("On Mode 4 - Random");
  eepromModeCheckAndUpdate(mode_flag);
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
  uint16_t i, j, k;
  int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
  while (mode_flag == 4)  // do this indefenitely or until mode changes
  {
    for (j = 0; j < 256; j++) // 5 cycles of all colors on wheel
    {
      loop();
      for (i = 0; i < NUM_LEDS; i++)
      {
        loop();
        ChangePalettePeriodically();
      }
      //FastLED.show();
      for (k = 0; k < 50; k++)  // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
      {
        delay(wait);
        loop();
      }
    }
  } return;
}

//Palette Select, Linearblend mode
void handle_mode5() {
  mode_flag = 5;
  Serial.println("On Mode 5 - Palette Picker");
  eepromModeCheckAndUpdate(mode_flag);
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
  uint16_t i, j, k;
  int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
  //mode_flag = 4;
  while (mode_flag == 5)  // do this indefenitely or until mode changes
  {
    for (j = 0; j < 256; j++) // 5 cycles of all colors on wheel
    {
      loop();
      for (i = 0; i < NUM_LEDS; i++)
      {
        loop();
        currentBlending = LINEARBLEND;
      }
      //FastLED.show();
      for (k = 0; k < 50; k++)  // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
      {
        delay(wait);
        loop();
      }
    }
  } return;
}

//Palette Select, Noblending mode
void handle_mode6() {
  mode_flag = 6;
  Serial.println("On Mode 6 - Palette Picker");
  eepromModeCheckAndUpdate(mode_flag);
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
  uint16_t i, j, k;
  int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
  //mode_flag = 4;
  while (mode_flag == 6)  // do this indefenitely or until mode changes
  {
    for (j = 0; j < 256; j++) // 5 cycles of all colors on wheel
    {
      loop();
      for (i = 0; i < NUM_LEDS; i++)
      {
        loop();
        currentBlending = NOBLEND;
      }
      //FastLED.show();
      for (k = 0; k < 50; k++)  // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
      {
        delay(wait);
        loop();
      }
    }
  } return;
}

void handle_cloud() {
  mode_flag = 5;
  Serial.println("On Mode Cloud - flag 5");
  eepromModeCheckAndUpdate(mode_flag);
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
  uint16_t i, j, k;
  int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
  //mode_flag = 4;
  while (mode_flag == 5)  // do this indefenitely or until mode changes
  {
    for (j = 0; j < 256; j++) // 5 cycles of all colors on wheel
    {
      loop();
      for (i = 0; i < NUM_LEDS; i++)
      {
        loop();
        currentPalette = CloudColors_p;
        }
      //FastLED.show();
      for (k = 0; k < 50; k++) // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
      {
        delay(wait);
        loop();
      }
    }
  } return;
}

void handle_party() {
  mode_flag = 5;
  Serial.println("On Mode Party - flag 5");
  eepromModeCheckAndUpdate(mode_flag);
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
  uint16_t i, j, k;
  int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
  //mode_flag = 4;
  while (mode_flag == 5)  // do this indefenitely or until mode changes
  {
    for (j = 0; j < 256; j++) // 5 cycles of all colors on wheel
    {
      loop();
      for (i = 0; i < NUM_LEDS; i++)
        {
          loop();
          currentPalette = PartyColors_p;
          }
      //FastLED.show();
      for (k = 0; k < 50; k++)  // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
        {
          delay(wait);
          loop();
          }
      }
  } return;
}

void handle_heat() {
  mode_flag = 5;
  Serial.println("On Mode Heat - flag 5");
  eepromModeCheckAndUpdate(mode_flag);
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
  uint16_t i, j, k;
  int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
  //mode_flag = 4;
  while (mode_flag == 5)  // do this indefenitely or until mode changes
  {
      for (j = 0; j < 256; j++) // 5 cycles of all colors on wheel
      {
          loop();
          for (i = 0; i < NUM_LEDS; i++)
          {
            loop();
            currentPalette = HeatColors_p;
          }
          //FastLED.show();
          for (k = 0; k < 50; k++)  // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
          {
            delay(wait);
            loop();
          }
      }
  } return;
}

void handle_ocean() {
  mode_flag = 5;
  Serial.println("On Mode Ocean - flag 5");
  eepromModeCheckAndUpdate(mode_flag);
  server.send ( 200, "text/html", "<SCRIPT language='JavaScript'>window.location='/';</SCRIPT>");
  uint16_t i, j, k;
  int wait = 10;  //DON'T ever set this more than '10'. Use the 'k' value in the loop below to increase delays. This prevents the watchdog timer timing out on the ESP8266
  while (mode_flag == 5)  // do this indefenitely or until mode changes
  {
    for (j = 0; j < 256; j++) // 5 cycles of all colors on wheel
    {
      loop();
      for (i = 0; i < NUM_LEDS; i++)
      {
        loop();
        currentPalette = OceanColors_p;
      }
      //FastLED.show();
      for (k = 0; k < 50; k++)  // Do ten loops of the 'wait' and service loop routine inbetween. Total wait = 10 x 'wait'. This prevents sluggishness in the browser html front end menu.
      {
        delay(wait);
        loop();
      }
    }
  } return;
}
	
