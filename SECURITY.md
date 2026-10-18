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

- Firmware sends only fixed key codes (F13–F24, media keys, one mute chord). No typed strings.
- Wi-Fi and Bluetooth are never initialised.
- Production USB serial exposes no command interpreter; bench commands exist only in the
  `bringup` build.
- Platform and libraries pinned to exact versions. CI actions pinned to commit SHAs.
