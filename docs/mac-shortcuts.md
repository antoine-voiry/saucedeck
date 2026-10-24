# Mac setup: deck keys via the Shortcuts app

The deck sends F13–F24 (preset `presets/mac-daily.json`, also the factory default).
macOS turns each F-key into an action with the built-in **Shortcuts** app. Nothing to install.
Works on any keyboard layout (F-keys are key codes, not text).

## One-time setup (per Mac)

For each row below:

1. Open **Shortcuts** → **+** (new shortcut). Name it after the key.
2. Add the action from the table (search the action list on the right).
3. Click **ⓘ** (Shortcut Details) → **Add Keyboard Shortcut** → press the deck key.
   The field should show the F-key (F13…F24).

| Deck key | Sends | Shortcuts action |
|---|---|---|
| 1 Screenshot | F13 | **Take Screenshot** (set to interactive / selection) |
| 2 Chrome | F14 | **Open App** → Google Chrome |
| 3 Gmail | F15 | **Open URLs** → `https://mail.google.com`, browser: Google Chrome (if the action offers no browser choice: **Open App** Chrome, then **Open URLs**) |
| 4 Telegram | F16 | **Open App** → Telegram |
| 5 Fusion 360 | F17 | **Open App** → Autodesk Fusion |
| 6 System Settings | F18 | **Open App** → System Settings |
| 7 Help | F19 | **Open App** → Tips |
| 8 YouTube | F20 | **Open URLs** → `https://www.youtube.com` |
| 9 Hide | F21 | **Hide App** → front app |
| 10 iMessage | F22 | **Open App** → Messages |
| 11 Weather | F23 | **Open App** → Weather |
| 12 Bambu Studio | F24 | **Open App** → BambuStudio |

## Loading the preset on the deck

Only needed if the deck's key map was changed. Open the Wi-Fi portal (hold both knobs 5 s),
browse to `http://saucedeck.local/param`, **Load preset** → `presets/mac-daily.json`, **Save**,
then press knob 1 on the deck to confirm.

## Notes

- Not bench-verified yet: F13–F24 capture in Shortcuts, the Chrome browser option in
  **Open URLs**, and **Take Screenshot** options. Check each when setting up.
- If a shortcut does not fire, check System Settings → Privacy & Security → Accessibility
  for Shortcuts / the shortcut, and that no other app grabs the same F-key.
