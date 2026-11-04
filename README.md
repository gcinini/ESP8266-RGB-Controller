# ESP8266 RGB Controller

Wi-Fi firmware for a WS2812B LED lamp built on a Wemos D1 Mini (ESP8266). You control it from a web page on your network or remotely through Adafruit IO (MQTT).

## Features

- **Web page** served by the lamp itself, works on phones and desktops: power, colour (using the browser's own colour picker), brightness, mode and palette.
- **Modes:** solid colour, colour fade, rainbow, random (a different palette every few seconds), palette modes with smooth or stepped blending, and a flickering candle.
- **Palettes:** Cloud, Ocean, Party, Heat, Forest, Lava.
- **Adafruit IO control** over encrypted MQTT (TLS, port 8883) for every mode and palette, including colour names (`Hot Pink`) or hex values (`#FF8800`), plus a status report (mode, colour, Wi-Fi, IP address, uptime).
- **Remembers its settings.** Colour, brightness, mode and palette are restored after a power cut.
- **Over-the-air (OTA) updates** over Wi-Fi, optionally password protected.
- **Keeps working when the internet is down.** The lamp and web page stay usable and Adafruit IO reconnects in the background.

## Repository layout

```
ESP8266-RGB-Web-Server/          the sketch (open this folder in the Arduino IDE)
├── ESP8266-RGB-Web-Server.ino   firmware
├── web_page.h                   the web interface (HTML/CSS/JS stored in flash)
├── mqtt_roots.h                 root certificates trusted for the encrypted Adafruit IO connection
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
| LEDs used by Candle mode | 14, counted from LED 0 | `CANDLE_LEDS`, `CANDLE_AT_LED_ZERO` |

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
2. In **Boards Manager**, install **esp8266** and select **LOLIN(WEMOS) D1 R2 & mini**. Under **Tools → CPU Frequency**, choose **160 MHz** (PlatformIO does this automatically).
3. In **Library Manager**, install **FastLED** and **PubSubClient** (by Nick O'Leary).
4. Open `ESP8266-RGB-Web-Server/ESP8266-RGB-Web-Server.ino` and upload it.

There's nothing else to upload: the web page is part of the firmware.

## Using it

After boot, the serial monitor shows the lamp's address, for example `http://192.168.1.50/` (or `http://ESP-lightbox1.local/`). Open it in a browser. The page refreshes itself every few seconds, so changes made through Adafruit IO show up there too. Its **Network** section shows the address, Wi-Fi signal, Adafruit IO connection and firmware version.

### Adafruit IO feeds

| Feed | Values |
|---|---|
| `espweb.onoff` or `espweb.button` | `ON`, `OFF`, `WHITE` |
| | modes: `SOLID`, `FADE`, `RAINBOW`, `RANDOM`, `SMOOTH` (or `PALETTE`), `STEPPED`, `CANDLE` |
| | palettes: `CLOUD`, `OCEAN`, `PARTY`, `HEAT`, `FOREST`, `LAVA` |
| `espweb.color` | a colour name (`Blue`, `Hot Pink`, `Warm White`, …) or hex (`#FF8800`) |
| `espweb.brightness` | `0`–`100` |
| `espweb.outtopic` | **published by the lamp:** a status report (see below) |

The lamp connects to `io.adafruit.com` on **port 8883 with TLS**, so your Adafruit IO key and messages are encrypted. It accepts the server only if its certificate is signed by one of the root certificates in `mqtt_roots.h`: DigiCert Global Root G2, which Adafruit IO uses today, or ISRG Root X1 (Let's Encrypt) as a fallback. Because certificate dates have to be checked, the lamp sets its clock from NTP before connecting.

For this to work through a firewall, the lamp needs outbound **TCP 8883** to the internet and **UDP 123** (NTP). If the serial monitor shows `TLS error … certificate`, Adafruit IO has probably moved to a different certificate authority: add that authority's root certificate to `mqtt_roots.h`. As a last resort, `#define MQTT_USE_TLS 0` in `secrets.h` switches back to unencrypted port 1883.

Commands are not case sensitive. Choosing a palette switches to a palette mode; `SOLID` goes back to the last colour.

The lamp publishes a status report to `espweb.outtopic` when it connects, about 1.5 seconds after anything changes (at most once every 5 seconds), and every 10 minutes:

```
Power: On
Mode: Palette, smooth
Palette: Ocean
Brightness: 40%
Wi-Fi: gc-dev (-63 dBm)
IP: 192.168.30.30
Host: ESP-lightbox1.local
Adafruit IO: encrypted (TLS)
Firmware: v3.3.0, up 2h 05m
```

If the lamp loses its connection, the report simply stops updating, so check the block's timestamp to see how fresh it is.

### Setting up the Adafruit IO dashboard

The free Adafruit IO plan allows 10 feeds and 30 messages a minute. This setup uses 5 feeds:

| Block | Feed | Settings |
|---|---|---|
| Toggle | `espweb.onoff` | On text/value `ON`, off text/value `OFF` |
| Slider | `espweb.brightness` | min `0`, max `100`, step `1` |
| Color Picker | `espweb.color` | |
| Multi-line Text | `espweb.outtopic` | the status report |
| Momentary Button (one per mode) | `espweb.button` | button text and press value: `SOLID`, `FADE`, `RAINBOW`, `RANDOM`, `SMOOTH`, `STEPPED`, `CANDLE`, `WHITE` |
| Momentary Button (one per palette) | `espweb.button` | button text and press value: `CLOUD`, `OCEAN`, `PARTY`, `HEAT`, `FOREST`, `LAVA` |

Leave each button's **release value** empty. Otherwise every press sends a second message, which uses up the message allowance and shows up as "unknown command" in the serial monitor.

The dashboard's controls don't move by themselves when you change the lamp from the web page. The status block is the place to see what the lamp is actually doing.

### Web API

Every endpoint is a plain GET request and answers with the current state as JSON, so you can also script the lamp or connect it to Home Assistant:

| Endpoint | Example |
|---|---|
| `/state` | `{"on":true,"mode":3,"palette":1,"brightness":128,"colour":"#ffb46b","palettes":[...],"host":"ESP-lightbox1","ip":"192.168.20.57","rssi":-63,"mqtt":true,"version":"3.2.0","uptime":"2h 05m"}` |
| `/power?on=1` or `?on=0` | turn on or off |
| `/colour?hex=ff8800` | solid colour (a colour name also works) |
| `/brightness?value=1-255` | brightness |
| `/mode?id=1-7` | 1 solid, 2 fade, 3 rainbow, 4 random, 5 palette smooth, 6 palette stepped, 7 candle |
| `/palette?id=0-5` | 0 Cloud, 1 Ocean, 2 Party, 3 Heat, 4 Forest, 5 Lava |

## Over-the-air (OTA) updates

Once the lamp has been flashed over USB, later updates can go over Wi-Fi:

```
pio run -e d1_mini_ota -t upload
```

Set `upload_port` in `platformio.ini` to the lamp's IP address (shown on its web page under **Network**), and give the lamp a fixed DHCP lease so the address doesn't change. A `.local` name only works when your PC and the lamp are on the same network: mDNS doesn't cross VLANs or subnets.

An OTA upload uses two connections:

| Direction | Protocol / port | Purpose |
|---|---|---|
| PC → lamp | UDP 8266 | PlatformIO invites the lamp to update |
| lamp → PC | TCP 8270 | the lamp downloads the firmware from your PC (fixed with `--host_port=8270` in `platformio.ini`) |

If the lamp is on a separate VLAN or behind a firewall, allow both, limited to the lamp's and PC's addresses, and allow TCP 8270 into the PC in Windows Firewall. On MikroTik, check the interfaces on the second rule: that traffic comes **in** on the lamp's VLAN and goes **out** to the PC's. For example:

```powershell
New-NetFirewallRule -DisplayName "ESP lamp OTA (PlatformIO)" -Direction Inbound -Protocol TCP -LocalPort 8270 -RemoteAddress <lamp IP> -Action Allow -Profile Private
```

A successful upload prints `Sending invitation`, `Waiting for device...`, then `Uploading: [====] 100%`. If it stops at `No response from device`, the UDP 8266 direction is blocked. If it stops after `Waiting for device...`, the TCP 8270 direction is blocked.

**Optional password.** Without one, anyone on your network can flash the lamp. To require a password:

1. In `secrets.h`, add `#define OTA_PASSWORD "your-password"`.
2. Set the same password as a Windows environment variable: `setx LIGHTBOX_OTA_PASSWORD "your-password"`. Then restart VS Code.
3. In `platformio.ini`, remove the `;` in front of the `--auth=…` line under `upload_flags`.
4. Upload once more over USB (or OTA without a password), because the running firmware only asks for the password after it has been updated.

## Security notes

- **Credentials stay out of git.** Wi-Fi and Adafruit IO details live only in `secrets.h`, which is git-ignored.
- **Don't publish compiled firmware.** A `firmware.bin` built from your `secrets.h` contains those credentials in readable form, so releases contain source code only.
- **Adafruit IO traffic is encrypted** (TLS on port 8883), and the server's certificate is checked.
- **OTA has no password by default.** Set `OTA_PASSWORD` (see above) if other people or devices share the lamp's network.
- **Older versions of this repository contained credentials in the code.** Those values were changed and then removed from the entire git history on 2026-11-04, so older tags now show `***REMOVED***` in their place.

## Versions

Releases are tagged with [Semantic Versioning](https://semver.org/). See the **Releases** page on GitHub for the notes for each version. To check out an older version:

```bash
git checkout v3.0.4
```

| Version | Highlights |
|---|---|
| 3.3.0 | Encrypted Adafruit IO connection (TLS, port 8883) with root-certificate checking |
| 3.2.0 | Candle mode; status report to Adafruit IO; every mode and palette controllable over MQTT; Network section on the web page; OTA across VLANs |
| 3.1.0 | New web interface using the browser's own colour picker (no jscolor, no LittleFS upload); firmware rewritten; MIT license |
| 3.0.4 | Modes rewritten so they no longer block: one-click mode changes, saved palettes, MQTT colour names and hex, non-blocking reconnect |
| 3.0.2 | Sketch moved into its own folder for the Arduino IDE; PlatformIO support; credentials moved to `secrets.h` |
| 3.0.0 – 3.0.1 | FastLED palettes, Adafruit IO (MQTT), OTA updates |
| 2.1.0 | First FastLED web-server version |

## License

This project is released under the [MIT License](LICENSE). The libraries compiled into the firmware are listed, with their licenses, in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Built with

- [FastLED](https://fastled.io/)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [ESP8266 core for Arduino](https://github.com/esp8266/Arduino)
- [Adafruit IO](https://io.adafruit.com/)

## Acknowledgements

Thanks to [giznoise](https://github.com/giznoise/Arduino-ESP8266-RGB-Web-Server), whose ESP8266 RGB lamp web server started this project, and to the forks by [Danorexic](https://github.com/Danorexic/Arduino-ESP8266-RGB-Web-Server) and [Makerspaceteam04](https://github.com/Makerspaceteam04/Arduino-ESP8266-RGB-Web-Server) that inspired the FastLED and MQTT versions.
