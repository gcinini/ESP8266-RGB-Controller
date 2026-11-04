# ESP8266 RGB Controller

A Wi-Fi web server for the ESP8266 that drives a WS2812B addressable RGB LED strip. You control it from a browser on your network, or remotely through Adafruit IO (MQTT). It's built for a Wemos D1 Mini inside a glowing "lightbox" lamp.

## Features

- **Web interface** with an on/off switch, a colour picker (jscolor), brightness, mode selection and palette selection
- **Modes:** fixed colour, colour fade, rainbow, random palette cycling, and palette animations with or without blending
- **Palettes:** Cloud, Party, Heat, Ocean
- **Remote control over MQTT** through Adafruit IO feeds (`espweb.onoff`, `espweb.color`, `espweb.brightness`, `espweb.button`)
- **Over-the-air (OTA) firmware updates.** The device shows up as `lightbox1` in the Arduino IDE port list.
- **Remembers its state.** Colour, brightness and mode are saved to EEPROM and restored on power-up.

## Repository layout

```
ESP8266-RGB-Web-Server/          <- current sketch (v3.x), open this in Arduino IDE
├── ESP8266-RGB-Web-Server.ino
├── html_pages.h                 web UI pages (stored in PROGMEM)
├── secrets.example.h            template for your credentials
├── secrets.h                    your credentials (git-ignored, create it yourself)
└── data/
    └── jscolor.js               served from LittleFS, must be uploaded separately
Circuit Construction Notes.txt
platformio.ini                   PlatformIO build settings (VS Code)
```

The sketch folder has the same name as its `.ino` file, which is what the Arduino IDE needs to open it.

## Hardware

| Item | Default | Where to change it |
|---|---|---|
| Board | Wemos D1 Mini (ESP8266) | |
| LED strip | WS2812B, GRB colour order | `LED_TYPE`, `COLOR_ORDER` |
| Number of LEDs | 30 | `NUM_LEDS` |
| Data pin | D4 | `DATA_PIN` |

See *Circuit Construction Notes.txt* for how the lamp is built. In short, the strip is wrapped around a tube about 1.5 inches across and placed inside an opaque jar so the LEDs blend into an even glow.

## Building

### 1. Install the ESP8266 board package

In **File → Preferences → Additional boards manager URLs**, add:

```
https://arduino.esp8266.com/stable/package_esp8266com_index.json
```

Then go to **Tools → Board → Boards Manager**, install **esp8266**, and select **LOLIN(WEMOS) D1 R2 & mini**.

### 2. Install the libraries

From **Library Manager**:

- **FastLED**
- **PubSubClient** (by Nick O'Leary)

The ESP8266 board package already includes the Wi-Fi, web server, mDNS, EEPROM, ArduinoOTA and LittleFS libraries.

### 3. Add your credentials

Copy `ESP8266-RGB-Web-Server/secrets.example.h` to `secrets.h` in the same folder, then fill in your Wi-Fi network and your Adafruit IO username and key. `secrets.h` is git-ignored, so your credentials never reach GitHub. If the file is missing, the build stops with a message telling you to create it.

### 4. Upload the sketch and the web files

1. Open `ESP8266-RGB-Web-Server/ESP8266-RGB-Web-Server.ino` and upload it, either over USB or over the air to `lightbox1`.
2. Upload the `data/` folder to LittleFS. Without this step the colour picker won't load. In Arduino IDE 2.x, install the [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload) plugin, close the Serial Monitor, then press `Ctrl+Shift+P` and run **Upload LittleFS to Pico/ESP8266/ESP32**.

### PlatformIO (VS Code)

`platformio.ini` in the repo root points PlatformIO at the same `ESP8266-RGB-Web-Server/` folder, so both tools build the same files. PlatformIO downloads the board package and libraries itself, so steps 1 and 2 are only needed for the Arduino IDE. Step 3 (`secrets.h`) is needed for both.

Open the repo folder in VS Code, then use the PlatformIO toolbar (the ✓ and → icons in the status bar) or run these commands in a **PlatformIO Core CLI** terminal (PlatformIO sidebar → Quick Access → Miscellaneous). A regular terminal won't find `pio` unless `%USERPROFILE%\.platformio\penv\Scripts` is on your PATH.

| Task | Command |
|---|---|
| Build | `pio run` |
| Upload over USB | `pio run -t upload` |
| Upload over Wi-Fi (OTA) | `pio run -e d1_mini_ota -t upload` |
| Upload `data/` to LittleFS | `pio run -t uploadfs` |
| Serial monitor | `pio device monitor` |

## Versions

Releases are tagged with [Semantic Versioning](https://semver.org/) (`v2.1.0`, `v3.0.0`, `v3.0.1`, ...). See the **Releases** and **Tags** pages on GitHub. To check out an older version:

```bash
git checkout v2.1.0
```

The original 2019 lamp sketch (`ESP8266_WiFi_RGB_Lamp_FINAL`, Adafruit NeoPixel with candle mode) was removed after v3.0.2. It is still in the history: check out `v3.0.2` or earlier to get it back.

## Ideas for the future

- Sunrise wake-up alarm, with brightness slowly rising to a chosen colour, using internet time
- Adjustable transition speed for the fade and rainbow modes
- More modes: strobe, disco, breathing
- Wi-Fi setup through a captive portal (AP mode) instead of hard-coded credentials
- Re-enable mDNS so the lamp is reachable at `lightbox1.local`

## Credits

Built on top of:

- https://github.com/giznoise/Arduino-ESP8266-RGB-Web-Server (original project)
- https://github.com/Makerspaceteam04/Arduino-ESP8266-RGB-Web-Server
- https://github.com/kakopappa/arduino-esp8266-alexa-wemo-switch
- [FastLED](https://fastled.io/), [PubSubClient](https://github.com/knolleary/pubsubclient), [jscolor](https://jscolor.com/)
