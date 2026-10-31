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

| Deck key | Sends | Action |
|---|---|---|
| 1 | F13 | Screenshot toolbar |
| 2 | F14 | Google Chrome |
| 3 | F15 | Gmail in Chrome |
| 4 | F16 | Telegram |
| 5 | F17 | Autodesk Fusion |
| 6 | F18 | System Settings |
| 7 | F19 | Tips (help) |
| 8 | F20 | YouTube in Chrome |
| 9 | Cmd+H | Hide front app (macOS itself, no Hammerspoon) |
| 10 | Ctrl+Opt+Cmd+F13 | Messages |
| 11 | Ctrl+Opt+Cmd+F14 | Weather |
| 12 | Ctrl+Opt+Cmd+F15 | Bambu Studio |

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
