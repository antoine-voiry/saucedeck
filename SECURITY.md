# Security policy

SAUCEDECK is a personal USB HID macro deck (ESP32-S3). It enumerates as a keyboard, so a
compromised firmware can type on the host. Treat firmware integrity issues as security issues.

## Supported versions

Only the latest commit on `main` is supported.

## Reporting a vulnerability

Do not open a public issue. Use GitHub private vulnerability reporting
(Security tab → "Report a vulnerability") or email the repository owner.
Expect a first reply within 7 days.

## Scope

- Firmware in `src/` and `lib/deck_core/`, build config in `platformio.ini`.
- CI in `.github/workflows/`.
- Out of scope: physical access attacks (anyone with USB access can reflash; Secure Boot is
  deliberately not enabled yet, see `docs/plans/2026-10-18-upstream-port.md`).

## Baseline

- Firmware sends only fixed key codes or validated keystrokes (modifiers + one key); it never
  types stored text. App / URL / Shortcut keys are only run by the Mac helper.
- Bluetooth is never linked. Wi-Fi exists only in `src/wifi_portal.cpp` (CI-checked), is off by
  default, starts only after a 5 s two-knob hold and turns off after 10 minutes. The Wi-Fi
  password is stored in plain flash (accepted risk). Key-map changes from the portal apply only
  after a knob press on the device. The setup hotspot uses a new random password each time.
- Production USB serial carries only framed, CRC-checked helper messages (HELLO / VOLUME / MIC
  in; ACTION / MIC events out). Host messages can only change what the deck displays. Bench
  commands exist only in the `bringup` build.
- Platform and libraries pinned to exact versions. CI actions pinned to commit SHAs.
