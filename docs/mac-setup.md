# Mac setup: deck keys via Hammerspoon

The deck sends plain keystrokes (preset `presets/mac-daily.json`). Hammerspoon (free,
open source) turns them into actions using `tools/mac/hammerspoon.lua`.

## Why not the Shortcuts app

Tried 2026-10-24 on macOS 26.7.1, bench-verified:
- The Shortcuts "Add Keyboard Shortcut" field recorded nothing, not even Ctrl+Option+Cmd+T
  from the Mac keyboard.
- No Deck shortcuts appeared as Services, so System Settings could not bind them either.
- macOS has no virtual keycodes for F21-F24: the deck sends them (HID usages 0x70-0x73), but a
  CGEventTap logger saw nothing. F13-F20 arrive fine.

## Setup

1. Install Hammerspoon (`brew install --cask hammerspoon` or hammerspoon.org).
2. `~/.hammerspoon/init.lua`:
   ```lua
   dofile(os.getenv("HOME") .. "/saucedeck/tools/mac/hammerspoon.lua")
   ```
3. Start Hammerspoon. A "SAUCEDECK keys loaded" alert confirms the config.
4. Load the preset on the deck: hold both knobs 5 s (Wi-Fi portal), open
   `http://saucedeck.local/param`, **Load preset** → `presets/mac-daily.json`, **Save**,
   then press knob 1 on the deck.

## Key map

| Deck key | Sends | Action | LCD label | Icon |
|---|---|---|---|---|
| 1 | F13 | Screenshot toolbar | Screenshot | `camera` |
| 2 | F14 | Google Chrome | Chrome | `globe` |
| 3 | F15 | Gmail in Chrome | Gmail | `envelope` |
| 4 | F16 | Telegram | Telegram | `plane` |
| 5 | F17 | Autodesk Fusion | Fusion 360 | `cube` |
| 6 | F18 | System Settings | Settings | `gear` |
| 7 | F19 | Tips (help) | Help | `question` |
| 8 | F20 | YouTube in Chrome | YouTube | `play` |
| 9 | Cmd+H | Hide front app (macOS itself, no Hammerspoon) | Hide app | `eye-off` |
| 10 | Ctrl+Opt+Cmd+F13 | Messages | iMessage | `chat` |
| 11 | Ctrl+Opt+Cmd+F14 | Weather | Weather | `weather` |
| 12 | Ctrl+Opt+Cmd+F15 | Bambu Studio | Bambu | `printer` |

Each tile shows a 24x24 icon above a one-line label (Plan 5, [spec](superpowers/specs/2026-11-08-key-icons-design.md)).
The portal has an icon field per key (`Key n icon`, empty = text-only tile); the preset sets them.

Edit actions in `tools/mac/hammerspoon.lua`, then Hammerspoon menu → Reload Config.
Bench-verified 2026-10-24: keys 1-9 and 11-12. Key 10: the deck sends Ctrl+Opt+Cmd+F13 (seen by a
CGEventTap logger) and the Hammerspoon binding is registered; Messages not coming up was a Mac-side
issue. Closed.

## Status helper

`tools/mac/saucedeck_helper.lua` talks to the deck over the USB serial port (115200) and feeds the
LCD: volume, mic, clock and six status cards. Mic knob input comes back the same way.

Add one line to `~/.hammerspoon/init.lua`, then Reload Config:

```lua
dofile(os.getenv("HOME") .. "/saucedeck/tools/mac/saucedeck_helper.lua")
```

Optional: `brew install ical-buddy` for the meeting card; macOS asks for Calendar access on first use.

Card slots (a source that fails or has nothing to show clears its slot):

| Slot | Shows | Refresh |
|---|---|---|
| 0 | Next meeting today (icalBuddy) | 60 s |
| 1 | Now playing (Spotify, then Music) | 5 s |
| 2 | CPU % and RAM % | 5 s |
| 3 | Battery % and charging | 60 s |
| 4 | Weather (wttr.in) | 15 min |
| 5 | Unread badge counts for Mail and Telegram (lsappinfo) | 30 s |

The helper connects only to a USB device whose product name is exactly `SAUCEDECK` (the bring-up
firmware enumerates as `SAUCEDECK-BRINGUP` and is ignored). `tools/bench.py` releases the port
automatically (10 min) before opening it. Before `pio device monitor`, run
`hs -c 'saucedeck.release(3600)'`; afterwards run `hs -c 'saucedeck.resume()'`.

Uploading: `pio run -t upload` calls `tools/release_helper.py`, which runs `hs -c "saucedeck.release()"`
when the `hs` CLI exists. The helper closes the port and stays away for 60 s. Without the `hs` CLI,
quit Hammerspoon or run `saucedeck.release()` in its console before flashing. `saucedeck.status()`
returns the link state.

The helper pause after release is 60 s. An upload longer than that could see the helper reopen the port; if so, quit Hammerspoon.

Tests (host, no Hammerspoon needed):

```sh
/opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua
~/.platformio/penv/bin/python -m unittest discover -s test/tools
```

Not bench-verified: needs the helper loaded in Hammerspoon and a deck on USB.
