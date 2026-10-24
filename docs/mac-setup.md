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
Bench-verified 2026-10-24: keys 4 and 8. Others pending the preset load.
