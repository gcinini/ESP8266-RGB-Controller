# Third-party notices

The source code in this repository is the project's own work, released under the
MIT License (see [LICENSE](LICENSE)). This repository does not contain any
third-party source files.

The firmware is built with the libraries below. They are downloaded when you
build (by PlatformIO or the Arduino IDE), not stored here, but their code is
compiled into the firmware binary. If you share a built `firmware.bin`, include
this file with it.

| Component | Used for | License |
|---|---|---|
| [FastLED](https://github.com/FastLED/FastLED) | Driving the LED strip, colour palettes | MIT |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MQTT connection to Adafruit IO | MIT |
| [Espalexa](https://github.com/Aircoookie/Espalexa) | Local Alexa control (Philips Hue emulation) | MIT |
| [ESP8266 Arduino core](https://github.com/esp8266/Arduino) | Wi-Fi, web server, OTA updates, EEPROM | LGPL 2.1 (with bundled components under their own licenses, including Espressif's NONOS SDK under the Espressif MIT License) |

---

## FastLED

```
The MIT License (MIT)

Copyright (c) 2013 FastLED

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## PubSubClient

```
Copyright (c) 2008-2020 Nicholas O'Leary

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## Espalexa

```
MIT License

Copyright (c) 2017 Christian Schwinne

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## ESP8266 Arduino core

The ESP8266 core for Arduino is licensed under the GNU Lesser General Public
License, version 2.1. The full license text and the licenses of the components
bundled with the core are in the core's repository:

- License: https://github.com/esp8266/Arduino/blob/master/LICENSE
- Credits and bundled components: https://github.com/esp8266/Arduino#license-and-credits

The core is linked into the firmware as a library. Its source code is available
from the repository above, and this project's own source is in this repository,
so anyone can rebuild the firmware against a modified version of the core.
