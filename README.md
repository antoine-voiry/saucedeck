# SAUCEDECK

USB HID macro deck built on an ESP32-S3 SuperMini: 12 keys, 2 rotary encoders,
a 2" ST7789 LCD and a WS2812B LED strip. It enumerates as a keyboard + media controller
(`SAUCEDECK`) on macOS and Windows.

> Based on **[SauceDeck](https://github.com/HotTabascoSauce/SauceDeck)** by
> [HotTabascoSauce](https://github.com/HotTabascoSauce) — see [Credits](#credits).

## Features

- 12 macro keys sending F13–F24, mapped to apps on the host (macOS: F13–F20 only, it drops F21–F24).
- Knob 1: volume, push = mute. Knob 2: track next/previous, push = mic mute.
- LCD shows per-key legends, icons and status; LED strip for state feedback.
- Key map editable over a temporary Wi-Fi setup portal, stored in flash.
- macOS helper (Hammerspoon) and presets: [docs/mac-setup.md](docs/mac-setup.md).
- Bring-up firmware that tests every wire and names the likely fault.

## Hardware

- ESP32-S3 SuperMini (ESP32-S3FH4R2, 4 MB flash, 2 MB PSRAM)
- MCP23017 I/O expander (keys + encoder switches), 2× EC11 encoders
- 2.0" ST7789 240×320 SPI LCD, WS2812B strip
- Printed enclosure: remixed SauceDeck v1.1 Base + key and LCD bezels in [hardware/](hardware/)
  (print the original Top from MakerWorld)

Pinout: [wiring.md](wiring.md) (mirrored in [include/pins.h](include/pins.h)).
Hardware notes and gotchas: [docs/kb/](docs/kb/).

## Build

Requires [PlatformIO](https://platformio.org/).

```sh
pio test -e native                          # unit tests on host
pio test -e sim                             # firmware against the simulated board
pio run -t upload                           # build + flash the deck firmware
pio run -e bringup -t upload && pio device monitor   # wiring test
```

If the board does not show up: hold BOOT, plug USB, release, then upload.

## Credits

SAUCEDECK started from **SauceDeck** by **HotTabascoSauce**:

- Firmware and concept: <https://github.com/HotTabascoSauce/SauceDeck>
- Enclosure / CAD (SauceDeck v1.1): <https://makerworld.com/en/models/3232541-saucedeck-v1-1#profileId-3678032>

The idea, the 12-key + 2-knob + LCD layout and the enclosure come from that project.
This repository is a heavy rework: new board (SuperMini + MCP23017), new pin map,
firmware rewritten from scratch (no upstream code copied), Wi-Fi key-map portal,
macOS helper, tests and simulator, and a modified enclosure. Thank you for the original
design.

## License

- Firmware, tools and docs: [MIT](LICENSE).
- Enclosure files in [hardware/](hardware/): derived from the SauceDeck v1.1 MakerWorld
  model and therefore licensed
  [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) — no commercial use,
  and remixes must keep the same license. See [hardware/README.md](hardware/README.md).
