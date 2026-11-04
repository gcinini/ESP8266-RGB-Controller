// secrets.example.h - template for your private credentials.
//
// Copy this file to secrets.h (same folder) and fill in real values.
// secrets.h is listed in .gitignore, so it never gets committed.

#pragma once

// Wi-Fi network the lamp joins
#define WIFI_SSID      "your-wifi-ssid"
#define WIFI_PASSWORD  "your-wifi-password"

// Adafruit IO (MQTT) - username and "Active Key" from io.adafruit.com
#define MQTT_USER      "your-adafruit-io-username"
#define MQTT_KEY       "your-adafruit-io-key"

// Optional: password for over-the-air updates. If you set one, PlatformIO needs
// it too - see the OTA section of README.md.
// #define OTA_PASSWORD   "choose-an-ota-password"
