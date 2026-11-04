# ESP8266 RGB Controller

Wi-Fi firmware for a WS2812B LED lamp built on a Wemos D1 Mini (ESP8266). You control it from a web page on your network or remotely through Adafruit IO (MQTT).

## Features

- **Web page** served by the lamp itself, works on phones and desktops: power, colour (using the browser's own colour picker), brightness, mode and palette.
- **Modes:** solid colour, colour fade, rainbow, random (a different palette every few seconds), and palette modes with smooth or stepped blending.
- **Palettes:** Cloud, Ocean, Party, Heat, Forest, Lava.
- **Adafruit IO control** over MQTT, including colour names (`Hot Pink`) or hex values (`#FF8800`).
- **Remembers its settings.** Colour, brightness, mode and palette are restored after a power cut.
- **Over-the-air (OTA) updates** to `lightbox1`.
- **Keeps working when the internet is down.** The lamp and web page stay usable and Adafruit IO reconnects in the background.

## Repository layout

```
ESP8266-RGB-Web-Server/          the sketch (open this folder in the Arduino IDE)
├── ESP8266-RGB-Web-Server.ino   firmware
├── web_page.h                   the web interface (HTML/CSS/JS stored in flash)
├── secrets.example.h            template for your credentials
└── secrets.h                    your credentials (git-ignored, create it yourself)
platformio.ini                   PlatformIO build settings (VS Code)
LICENSE                          MIT license for this project
THIRD_PARTY_NOTICES.md           licenses of the libraries compiled into the firmware
```

## Hardware

| Item | Default | Where to change it |
|---|---|---|
| Board | Wemos D1 Mini (ESP8266) | |
| LED strip | WS2812B, GRB colour order | `LED_TYPE`, `LED_ORDER` |
| Number of LEDs | 35 | `LED_COUNT` |
| Data pin | D4 | `LED_PIN` |

The lamp looks best with the strip wound in a spiral around a tube about 4 cm (1.5 in) across, inside a frosted or opaque jar. The diffuser blends the individual LEDs into an even glow, and the spiral makes the rainbow and palette animations travel smoothly up the lamp.

## Building

### 1. Add your credentials

Copy `ESP8266-RGB-Web-Server/secrets.example.h` to `secrets.h` in the same folder, then fill in your Wi-Fi network and your Adafruit IO username and key. `secrets.h` is git-ignored. If the file is missing, the build stops with a message telling you to create it.

### 2a. PlatformIO (VS Code)

Open the repo folder in VS Code. PlatformIO downloads the board package and the libraries automatically. Use the toolbar (✓ to build, → to upload) or run these commands in a **PlatformIO Core CLI** terminal:

| Task | Command |
|---|---|
| Build | `pio run` |
| Upload over USB | `pio run -t upload` |
| Upload over Wi-Fi (OTA) | `pio run -e d1_mini_ota -t upload` |
| Serial monitor | `pio device monitor` |

### 2b. Arduino IDE

1. In **File → Preferences → Additional boards manager URLs**, add `https://arduino.esp8266.com/stable/package_esp8266com_index.json`.
2. In **Boards Manager**, install **esp8266** and select **LOLIN(WEMOS) D1 R2 & mini**.
3. In **Library Manager**, install **FastLED** and **PubSubClient** (by Nick O'Leary).
4. Open `ESP8266-RGB-Web-Server/ESP8266-RGB-Web-Server.ino` and upload it.

There's nothing else to upload: the web page is part of the firmware.

## Using it

After boot, the serial monitor shows the lamp's address, for example `http://192.168.1.50/`. Open it in a browser. The page refreshes itself every few seconds, so changes made through Adafruit IO show up there too.

### Adafruit IO feeds

| Feed | Values |
|---|---|
| `espweb.onoff` or `espweb.button` | `ON`, `OFF`, `FADE`, `RAINBOW`, `RANDOM`, `WHITE` |
| `espweb.color` | a colour name (`Blue`, `Hot Pink`, `Warm White`, …) or hex (`#FF8800`) |
| `espweb.brightness` | `0`–`100` |

Each time the lamp connects, it publishes `hello world` to `espweb.outtopic`, which is a quick way to check that it's online.

### Web API

Every endpoint is a plain GET request and answers with the current state as JSON, so you can also script the lamp or connect it to Home Assistant:

| Endpoint | Example |
|---|---|
| `/state` | `{"on":true,"mode":3,"palette":1,"brightness":128,"colour":"#ffb46b","palettes":[...]}` |
| `/power?on=1` or `?on=0` | turn on or off |
| `/colour?hex=ff8800` | solid colour (a colour name also works) |
| `/brightness?value=1-255` | brightness |
| `/mode?id=1-6` | 1 solid, 2 fade, 3 rainbow, 4 random, 5 palette smooth, 6 palette stepped |
| `/palette?id=0-5` | 0 Cloud, 1 Ocean, 2 Party, 3 Heat, 4 Forest, 5 Lava |

## Versions

Releases are tagged with [Semantic Versioning](https://semver.org/). See the **Releases** and **Tags** pages on GitHub. To check out an older version:

```bash
git checkout v3.0.4
```

## License

This project is released under the [MIT License](LICENSE). The libraries compiled into the firmware are listed, with their licenses, in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Built with

- [FastLED](https://fastled.io/)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [ESP8266 core for Arduino](https://github.com/esp8266/Arduino)
- [Adafruit IO](https://io.adafruit.com/)
