// secrets.example.h - template for your private credentials.
//
// Copy this file to secrets.h (same folder) and fill in real values.
// secrets.h is listed in .gitignore, so it never gets committed.
//
// Only passwords go here. Everything else you might change when setting up
// a lamp (LED count, pin, names, the broker's address...) is in config.h.
// If you have several lamps on the same Wi-Fi and broker, they can all share
// this one secrets.h.

#pragma once

// Wi-Fi network the lamp joins
#define WIFI_SSID      "your-wifi-ssid"
#define WIFI_PASSWORD  "your-wifi-password"

// Login for your MQTT broker (Mosquitto), created with mosquitto_passwd -
// see "Home Assistant" in README.md. Give the lamp its own user rather than
// reusing Home Assistant's.
#define MQTT_USER      "bedlight"
#define MQTT_PASSWORD  "your-mqtt-password"

// Optional: password for over-the-air updates. If you set one, PlatformIO needs
// it too - see the OTA section of README.md.
// #define OTA_PASSWORD   "choose-an-ota-password"
