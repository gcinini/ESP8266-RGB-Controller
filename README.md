# ESP8266 RGB Controller

Wi-Fi firmware for a WS2812B LED lamp built on a Wemos D1 Mini (ESP8266). You control it from a web page on your network, by voice with an Amazon Echo (Alexa), or from Home Assistant, which finds it by itself through MQTT discovery. Everything runs on your own network, with no cloud service.

## Features

- **Web page** served by the lamp itself, works on phones and desktops: power, colour (using the browser's own colour picker), brightness, mode and palette.
- **Modes:** solid colour, colour fade, rainbow, random (a different palette every few seconds), palette modes with smooth or stepped blending, and a flickering candle.
- **Palettes:** Cloud, Ocean, Party, Heat, Forest, Lava.
- **Alexa voice control**, directly over your home network: no Alexa skill, account linking or cloud service needed. The lamp appears in the Alexa app as a colour bulb.
- **Home Assistant integration** over MQTT with automatic discovery: a light with brightness, colour and every mode as an effect, a palette selector, and Wi-Fi signal and IP address sensors.
- **Remembers its settings.** Colour, brightness, mode and palette are restored after a power cut.
- **Over-the-air (OTA) updates** over Wi-Fi, optionally password protected.
- **Keeps working on its own.** If the broker or Home Assistant is down, the web page and Alexa keep working, and MQTT reconnects in the background.

## Repository layout

```
ESP8266-RGB-Web-Server/          the sketch (open this folder in the Arduino IDE)
├── ESP8266-RGB-Web-Server.ino   firmware, with comments explaining how it works
├── config.h                     settings for each lamp (LED count, pin, names, MQTT broker...)
├── web_page.h                   the web interface (HTML/CSS/JS stored in flash)
├── secrets.example.h            template for your credentials
└── secrets.h                    your credentials (git-ignored, create it yourself)
platformio.ini                   PlatformIO build settings (VS Code)
LICENSE                          MIT license for this project
THIRD_PARTY_NOTICES.md           licenses of the libraries compiled into the firmware
```

## Hardware

| Item | Lamp 1 | Setting in `config.h` |
|---|---|---|
| Board | Wemos D1 Mini (ESP8266) | |
| LED strip | WS2812B, GRB colour order | `LED_TYPE`, `LED_ORDER` |
| Number of LEDs | 34 | `LED_COUNT` |
| Data pin | D4 | `LED_PIN` |
| LEDs used by Candle mode | 14, counted from LED 0 | `CANDLE_LEDS`, `CANDLE_AT_LED_ZERO` |
| Power limit | 2000 mA | `MAX_MILLIAMPS` |

Each WS2812B LED can draw about 60 mA at full white, so 34 LEDs can need about 2 A. If your power supply is smaller, set `MAX_MILLIAMPS` to what it can deliver (for example `1500` for a 2 A supply) and the lamp dims itself just enough to stay under it.

The lamp looks best with the strip wound in a spiral around a tube about 4 cm (1.5 in) across, inside a frosted or opaque jar. The diffuser blends the individual LEDs into an even glow, and the spiral makes the rainbow and palette animations travel smoothly up the lamp.

## Configuration

All the settings you might change for a particular lamp are in **`ESP8266-RGB-Web-Server/config.h`**, with a comment explaining each one. Passwords and keys are not in it; they go in `secrets.h`.

`config.h` has one block of settings per lamp, numbered by `LAMP_ID`:

| Setting | What it is |
|---|---|
| `DEVICE_NAME` | Network name (hostname), used for OTA and `<name>.local`. Must be unique per lamp |
| `LAMP_NAME` | The friendly name: the device in Home Assistant, the name you say to Alexa, the web page title (`Bedlight` for lamp 1). Must be unique per lamp |
| `MQTT_TOPIC` | Start of the lamp's MQTT topics (`bedlight` for lamp 1). Must be unique per lamp |
| `LED_COUNT`, `LED_PIN`, `LED_TYPE`, `LED_ORDER` | The LED strip |
| `CANDLE_LEDS`, `CANDLE_AT_LED_ZERO` | Size and position of the Candle mode flame |

Below the lamp blocks are settings shared by every lamp: power limit, starting colour and brightness, animation speed, Alexa on/off (`ENABLE_ALEXA`), and the MQTT broker's address (`MQTT_HOST`).

### More than one lamp

1. In `config.h`, fill in the `LAMP_ID == 2` block: a different `DEVICE_NAME`, `LAMP_NAME` and `MQTT_TOPIC`, and its LED count.
2. **PlatformIO:** in `platformio.ini`, remove the `;` from the `lamp2_usb` and `lamp2_ota` sections and set lamp 2's IP address. Then upload with `pio run -e lamp2_usb -t upload` (first time) or `pio run -e lamp2_ota -t upload`.
   **Arduino IDE:** change `#define LAMP_ID 1` at the top of `config.h` to `2` before uploading to lamp 2, and back to `1` for lamp 1.
3. If the broker is on another VLAN, allow TCP 1883 from lamp 2's address too. The lamp appears in Home Assistant by itself; ask Alexa to discover devices for it.

Both lamps can share the same `secrets.h` (Wi-Fi and broker login). Each needs its own `MQTT_TOPIC`: two lamps on the same topic would both react to every command, and their state reports would overwrite each other.

## Building

### 1. Add your credentials

Copy `ESP8266-RGB-Web-Server/secrets.example.h` to `secrets.h` in the same folder, then fill in your Wi-Fi network and the lamp's MQTT broker login (see [Home Assistant](#home-assistant)). `secrets.h` is git-ignored. If the file is missing, the build stops with a message telling you to create it.

### 2a. PlatformIO (VS Code)

Open the repo folder in VS Code. PlatformIO downloads the board package and the libraries automatically. Use the toolbar (✓ to build, → to upload) or run these commands in a **PlatformIO Core CLI** terminal:

| Task | Command |
|---|---|
| Build | `pio run` |
| Upload lamp 1 over USB | `pio run -t upload` |
| Upload lamp 1 over Wi-Fi (OTA) | `pio run -e d1_mini_ota -t upload` |
| Serial monitor | `pio device monitor` |

### 2b. Arduino IDE

1. In **File → Preferences → Additional boards manager URLs**, add `https://arduino.esp8266.com/stable/package_esp8266com_index.json`.
2. In **Boards Manager**, install **esp8266** and select **LOLIN(WEMOS) D1 R2 & mini**. Under **Tools → CPU Frequency**, choose **160 MHz** (PlatformIO does this automatically).
3. In **Library Manager**, install **FastLED**, **PubSubClient** (by Nick O'Leary), **Espalexa** (by Christian Schwinne) and **ArduinoJson** (by Benoit Blanchon).
4. Open `ESP8266-RGB-Web-Server/ESP8266-RGB-Web-Server.ino` and upload it.

There's nothing else to upload: the web page is part of the firmware.

## Using it

After boot, the serial monitor shows the lamp's address, for example `http://192.168.30.30/` (or `http://ESP-bedlight.local/`). Open it in a browser. The page refreshes itself every few seconds, so changes made by Alexa or Home Assistant show up there too. The page heading shows the lamp's `LAMP_NAME`, and its **Network** section shows the address, Wi-Fi signal, Home Assistant (MQTT) connection, Alexa name and firmware version.

### Home Assistant

The lamp talks to Home Assistant through an **MQTT broker** on your own network, and uses Home Assistant's **MQTT discovery**: once the lamp connects, it appears in Home Assistant by itself as a device with these entities:

| Entity | What it does |
|---|---|
| `light.bedlight` | On/off, brightness and colour. The modes are listed as **effects**: Solid colour, Colour fade, Rainbow, Random, Palette smooth/stepped, Candle |
| `select.bedlight_palette` | Chooses the palette (Cloud, Ocean, Party, Heat, Forest, Lava); this also switches to a palette mode |
| `sensor.bedlight_wi_fi_signal` | Wi-Fi signal in dBm (diagnostic) |
| `sensor.bedlight_ip_address` | The lamp's IP address (diagnostic) |

The entity names come from `LAMP_NAME` in `config.h`. When the lamp is unplugged or restarting, Home Assistant shows it as **unavailable** rather than keeping a stale state.

Everything stays on your network: no cloud service is involved. To control the lamp from outside your home, use whatever remote access you already have for Home Assistant (Tailscale, Home Assistant Cloud and so on).

#### 1. Run an MQTT broker (Mosquitto)

- **Home Assistant OS:** install the **Mosquitto broker** app (add-on) from the add-on store, then create a Home Assistant user for the lamp (Settings → People → Users) and use that login in `secrets.h`.
- **Home Assistant Container** (Docker) has no add-ons, so run Mosquitto as its own container. Add it to the same Compose file as Home Assistant:

```yaml
  mosquitto:
    container_name: mosquitto
    image: eclipse-mosquitto:2.1.2-alpine
    restart: unless-stopped
    network_mode: host                 # listens on port 1883 of the host
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
    security_opt:
      - no-new-privileges:true
```

Create `mosquitto/config/mosquitto.conf` next to the Compose file:

```
listener 1883
allow_anonymous false
password_file /mosquitto/config/passwd
persistence true
persistence_location /mosquitto/data/
log_dest stdout
```

Then create two logins: one for Home Assistant and one for the lamp. Each command asks for a password; `-c` creates the file, so use it only for the first:

```bash
docker compose run --rm mosquitto mosquitto_passwd -c /mosquitto/config/passwd homeassistant
docker compose run --rm mosquitto mosquitto_passwd /mosquitto/config/passwd bedlight
docker compose up -d mosquitto
docker compose logs mosquitto        # should end with "mosquitto version 2.1.2 running"
```

#### 2. Connect Home Assistant to the broker

In Home Assistant: **Settings → Devices & services → Add integration → MQTT**. Broker `127.0.0.1` (Home Assistant and Mosquitto share the host's network), port `1883`, and the `homeassistant` login. Leave discovery on (the default).

#### 3. Point the lamp at the broker

- In `config.h`, set `MQTT_HOST` to the broker's IP address (a `.local` name won't work across VLANs).
- In `secrets.h`, set `MQTT_USER` and `MQTT_PASSWORD` to the lamp's login.
- Upload the firmware. The serial monitor shows `MQTT: connecting to … connected`, then `MQTT: announced to Home Assistant`.

The lamp then appears under **Settings → Devices & services → MQTT**. If it's on a separate VLAN from the broker, allow **TCP 1883 from the lamp to the broker** in your firewall. On MikroTik, for example (the traffic comes **in** on the lamp's VLAN and goes **out** to the broker's):

```
/ip firewall filter add chain=forward action=accept protocol=tcp \
    src-address=192.168.30.30 dst-address=192.168.10.61 dst-port=1883 \
    in-interface=vlan30-iot out-interface=vlan10-trusted \
    comment="MQTT: bedlight to Home Assistant broker" \
    place-before=[find where comment~"Drop VLAN30"]
```

If it doesn't connect, the serial monitor's `failed (state …)` number says why: `-2` means the broker can't be reached (wrong address or a firewall in the way), `4` means a wrong user or password, `5` means the user isn't allowed.

#### MQTT topics

For scripts, or to watch with a tool like MQTT Explorer. `bedlight` is `MQTT_TOPIC` from `config.h`:

| Topic | Direction | Contents |
|---|---|---|
| `bedlight/set` | to the lamp | JSON, e.g. `{"state":"ON","brightness":128}`, `{"color":{"r":255,"g":0,"b":0}}`, `{"effect":"Candle"}`, `{"state":"OFF"}` |
| `bedlight/palette/set` | to the lamp | a palette name, e.g. `Ocean` |
| `bedlight/state` | from the lamp (retained) | `{"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":255,"g":180,"b":107},"effect":"Candle","palette":"Ocean","rssi":-63,"ip":"192.168.30.30"}` |
| `bedlight/availability` | from the lamp (retained) | `online`, or `offline` (sent by the broker if the lamp disappears) |
| `homeassistant/…/config` | from the lamp (retained) | the discovery messages |

The lamp reports its state a quarter of a second after any change and once a minute, and announces itself again whenever Home Assistant restarts.

### Alexa

The lamp pretends to be a Philips Hue colour bulb (using the [Espalexa](https://github.com/Aircoookie/Espalexa) library), so an Amazon Echo finds it and controls it **directly over your home network**. There's no Alexa skill to enable and no cloud service in between, so it keeps working even when Home Assistant or the internet is down.

**Setting it up**

1. Choose the name in `config.h` with `LAMP_NAME` (`Bedlight` for lamp 1). Pick something Alexa hears easily and that no other device uses.
2. Upload the firmware. The serial monitor shows `Alexa: ready as "Bedlight"`.
3. Say **"Alexa, discover devices"** (or in the Alexa app: **Devices → + → Add Device → Other → Discover devices**). After about 45 seconds, Alexa reports a new light.

**What you can say**

| Say | The lamp |
|---|---|
| "Alexa, turn on the Bedlight" / "turn off" | switches on in its last mode (Candle, Rainbow…), or off |
| "Alexa, set the Bedlight to 40%" | sets the brightness |
| "Alexa, brighten / dim the Bedlight" | changes the brightness |
| "Alexa, set the Bedlight to blue" (or pink, purple, white…) | Solid colour mode in that colour |

Modes and palettes aren't available by voice, because a Hue bulb has no such setting. You can still put the lamp in a group or a routine in the Alexa app, like any other light. Changes made from the web page or Home Assistant show up in the Alexa app.

**Alexa and Home Assistant together.** The lamp talks to each of them directly, so both work at the same time. If you ever expose Home Assistant's lights to Alexa (for example through Home Assistant Cloud), leave this lamp out of that, or Alexa will list it twice.

**Requirements and troubleshooting**

- **The Echo and the lamp must be on the same network (VLAN / subnet).** Discovery uses a multicast search (SSDP, UDP 1900) that routers don't pass between networks. After discovery, the Echo sends commands to the lamp's web server on TCP port 80.
- **Client isolation must be off** on that Wi-Fi network (it may be called "AP isolation" or, on MikroTik, "client-to-client forwarding"). With it on, the Echo can't reach the lamp at all.
- **Which Echo:** first-generation Echos and the Echo Dot 2 are the most reliable at finding emulated Hue bulbs. Some newer Echos find them only now and then, or not at all. Discovering again sometimes helps.
- **Discovery finds nothing?** Open `http://<lamp IP>/espalexa` in a browser. It lists the device the lamp is offering. If that page loads but discovery still fails, check the two points above.
- **Renamed the lamp?** In the Alexa app, delete the old device and discover again.
- **Turning it off:** set `ENABLE_ALEXA` to `0` in `config.h`. The firmware is then smaller too.

### Web API

The `/espalexa` and `/description.xml` pages, and requests under `/api`, belong to the Alexa support described above.

Every other endpoint is a plain GET request and answers with the current state as JSON, so you can also script the lamp:

| Endpoint | Example |
|---|---|
| `/state` | `{"on":true,"mode":3,"palette":1,"brightness":128,"colour":"#ffb46b","palettes":[...],"name":"Bedlight","host":"ESP-bedlight","ip":"192.168.30.30","rssi":-63,"mqtt":true,"broker":"192.168.10.61","heap":30000,"alexa":"Bedlight","version":"4.0.0","uptime":"2h 05m"}` |
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

- **Credentials stay out of git.** Wi-Fi and MQTT broker passwords live only in `secrets.h`, which is git-ignored.
- **Don't publish compiled firmware.** A `firmware.bin` built from your `secrets.h` contains those credentials in readable form, so releases contain source code only.
- **MQTT on your network is not encrypted.** The lamp logs in to the broker with a password, but MQTT on port 1883 sends it and the messages as plain text. That's the usual setup for a home network; limit who can reach the broker with your firewall (the example rule above allows only the lamp's address). Give the lamp its own broker login rather than sharing Home Assistant's.
- **Anyone on the lamp's network can control it,** through the web page, the web API or the Alexa (Hue) interface. None of these ask for a password, so keep the lamp on a network you trust, such as a separate IoT VLAN.
- **OTA has no password by default.** Set `OTA_PASSWORD` (see above) if other people or devices share the lamp's network.
- **Older versions of this repository contained credentials in the code.** Those values were changed and then removed from the entire git history on 2026-11-04, so older tags now show `***REMOVED***` in their place.

## Versions

Releases are tagged with [Semantic Versioning](https://semver.org/). See the **Releases** page on GitHub for the notes for each version. To check out an older version:

```bash
git checkout v3.0.4
```

| Version | Highlights |
|---|---|
| 4.0.0 | **Adafruit IO removed; Home Assistant integration** through a local MQTT broker with automatic discovery (light with effects, palette selector, diagnostic sensors, availability). `LAMP_NAME` and `MQTT_TOPIC` replace `ALEXA_NAME` and `FEED_GROUP`; `secrets.h` uses `MQTT_USER`/`MQTT_PASSWORD`. No more TLS certificates or NTP. Uses ArduinoJson |
| 3.5.0 | Local Alexa control (Hue bulb emulation with Espalexa): on/off, brightness and colour by voice; Alexa name on the web page and status report |
| 3.4.1 | Settings moved to `config.h`, with support for several lamps; optional power limit; web page shows the lamp's name; code comments written for beginners |
| 3.3.0 | Encrypted Adafruit IO connection (TLS, port 8883) with root-certificate checking |
| 3.2.0 | Candle mode; status report to Adafruit IO; every mode and palette controllable over MQTT; Network section on the web page; OTA across VLANs |
| 3.1.0 | New web interface using the browser's own colour picker (no jscolor, no LittleFS upload); firmware rewritten; MIT license |
| 3.0.4 | Modes rewritten so they no longer block: one-click mode changes, saved palettes, MQTT colour names and hex, non-blocking reconnect |
| 3.0.2 | Sketch moved into its own folder for the Arduino IDE; PlatformIO support; credentials moved to `secrets.h` |
| 3.0.0 – 3.0.1 | FastLED palettes, Adafruit IO (MQTT), OTA updates |
| 2.1.0 | First FastLED web-server version |

## Learning from the code

The sketch is commented for people learning C and C++, with an explanation the first time each idea appears. Open `ESP8266-RGB-Web-Server.ino` and start with **HOW TO READ THIS FILE** at the top. A good order to follow it in:

1. **`setup()` and `loop()`** (section 3): what runs once at start-up and what repeats forever.
2. **`lampChanged()`** (section 4): every change goes through it.
3. **`drawFrame()`** (section 6): the non-blocking timer, and how a frame is drawn.
4. **`onMqttMessage()`** and **`publishDiscovery()`** (section 8): how a Home Assistant command becomes a change, and how the lamp describes itself to Home Assistant.
5. **`startWebServer()`** (section 10), and the JavaScript in `web_page.h`: how the web page talks to the lamp.
6. **`onAlexaCommand()`** (section 9): how a voice command becomes the same kind of change.

Then try small changes, such as the colour of the candle flame in `drawCandle()` or a new name in `COLOUR_NAMES`, and upload them over OTA.

## License

This project is released under the [MIT License](LICENSE). The libraries compiled into the firmware are listed, with their licenses, in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Built with

- [FastLED](https://fastled.io/)
- [PubSubClient](https://github.com/knolleary/pubsubclient)
- [Espalexa](https://github.com/Aircoookie/Espalexa)
- [ESP8266 core for Arduino](https://github.com/esp8266/Arduino)
- [ArduinoJson](https://arduinojson.org/)
- [Home Assistant](https://www.home-assistant.io/) and [Eclipse Mosquitto](https://mosquitto.org/)

## Acknowledgements

Thanks to [giznoise](https://github.com/giznoise/Arduino-ESP8266-RGB-Web-Server), whose ESP8266 RGB lamp web server started this project, and to the forks by [Danorexic](https://github.com/Danorexic/Arduino-ESP8266-RGB-Web-Server) and [Makerspaceteam04](https://github.com/Makerspaceteam04/Arduino-ESP8266-RGB-Web-Server) that inspired the FastLED and MQTT versions.
