# Port plan: upstream SauceDeck capabilities

Source: <https://github.com/HotTabascoSauce/SauceDeck/tree/main/src> (`main.cpp`, read 2026-10-18).
Scope: port **capabilities**, not the design. Their pin map, direct-GPIO keys, Wokwi files,
LVGL shim, Windows launch macros and bitmaps are not ported.

## Decisions (2026-10-18)

| Topic | Decision |
| --- | --- |
| Macro keys | Keep F13–F24. Apps are bound on the host (Shortcuts / Raycast / BTT). |
| Knob 1 | Speaker: VOL± per detent, push = MUTE, open-loop level + mute shown on LCD. |
| Knob 2 | Rotation keeps next/prev track. Push = mic-mute shortcut (configurable, default Cmd+Shift+M) and toggles a mic-mute indicator. No fake mic level. |
| LCD driver | Adafruit ST7789 + Adafruit GFX. |
| Icons | Own simple icons drawn with GFX primitives. Upstream has no LICENSE, so no copying. |

## Capability map

| # | Upstream capability | Port |
| --- | --- | --- |
| C1 | 25 ms key debounce | New, `deck_core` (local firmware has none today) |
| C2 | 12 macro keys | Already done (F13–F24) |
| C3 | Knob 1 volume with tracked level | New level model, `deck_core` |
| C4 | Knob 1 push mute + state | Add mute state |
| C5 | Knob 2 mic mute shortcut | New, configurable chord |
| C6 | Both pushes = sync levels to 0 | New chord detector, suppresses single-push mute |
| C7 | LCD volume screen (bars, text, icons) | New, partial redraw (upstream clears whole screen → flicker) |
| C8 | Boot splash | New, non-blocking (upstream `delay(5000)` delays USB enumeration) |

## Phases

Each phase: failing test → minimal code → refactor → firmware builds. One commit per phase.
Done per phase = `pio test -e native` green and `pio run -e saucedeck -e bringup` SUCCESS, no new warnings.

1. **Debounce** — `lib/deck_core/src/debounce.h`, suite `test_core`.
   16-bit mask debouncer, time passed in (`update(raw, nowMs)`). Tests: bounce shorter than 25 ms
   ignored, stable press → one event, release, independent bits, `millis()` wrap.
   Wire: `scanButtons()` feeds debounced mask to `diffButtons`.
   **Done 2026-10-18** (8 tests). Not bench-verified.
   Found: MCP bus drop while a key is held → stuck F-key on host. Fixed in phase 4.
2. **Volume / mute model** — `lib/deck_core/src/volume_state.h`, new suite `test_volume`.
   Level 0–100, step per detent configurable (host-dependent, see risks), clamp, mute toggle,
   rotate-up while muted unmutes (matches macOS behaviour, unverified), sync reset.
   **Done 2026-10-18** (11 tests). Level kept in host steps (macOS 16, unverified), % derived.
3. **Knob chord** — in `volume_state` or `knob_chord.h`.
   Both switches held → `Sync` once until both released; single press → `Mute` only if the
   other switch was not involved. Tests cover press order and release order.
   **Done 2026-10-18** (8 tests, `knob_chord.h`). Single knob fires on **release** (upstream fired
   on press, so a two-knob press could mute before syncing).
4. **Action map** — `lib/deck_core/src/actions.h`.
   Deck events → action list (`Key`, `Consumer`, `Chord{mods,key}`). `src/main.cpp` only executes
   actions over HID. Mic-mute chord is one constant in `src/main.cpp`.
   **Done 2026-10-18** (`deck.h`, 14 tests in `test_deck`). Includes stuck-key fix: bus lost →
   `ReleaseAll` once, knob state reset without firing, held keys press again when bus returns.
5. **Screen model** — `lib/deck_core/src/ui_layout.h`.
   Level → bar fill height, dirty flags (which bar/icon changed), so glue redraws only that region.
   Pure numbers, tested.
   **Done 2026-10-18** (`ui_layout.h`, 13 tests in `test_ui`). Muted bar keeps its level, drawn grey.
6. **LCD glue** — `src/display.cpp/.h`, `lib_deps` Adafruit ST7789 + GFX in `[env:saucedeck]`.
   Splash (time-boxed, drawn after `USB.begin()`), two bars + level text, speaker/mic icons with
   mute cross. ~~Blocked on BL current measurement~~ owner accepted risk 2026-10-18: GPIO9 with
   reduced drive strength, measure later.
   **Built + flashed 2026-10-18**, not bench-verified. Libs pinned (GFX 1.12.6, BusIO 1.17.4,
   ST7789 1.11.0, seesaw 1.7.9). SPI 20 MHz, rotation constant `kRotation`.
6b. **LED feedback** (added 2026-10-18, owner request; not in upstream) — `led_effects.h`
   (16 tests, `test_led`) + `src/leds.cpp`. Per-action flash (key hue, mute amber, mic red,
   track white, sync green, bus loss orange), knob-1 volume gauge 1.5 s, dim red idle while mic
   muted. Frame power-limited to 250 mA estimate. `kLedCount` = 16 placeholder (>12, exact count
   unknown). Flashed, not bench-verified. 3.3 V data margin still open (risk list).
7. **Encoder ISR** (if bench shows missed detents during redraw) — GPIO change interrupt on
   GPIO1–4 feeding `QuadDecoder`, main loop reads accumulated steps.
8. **Bench** — flash production firmware, check Mac enumeration as `SAUCEDECK`, every key, both
   knobs, mute/sync, screen. Log in `docs/bringup/`, facts into `docs/kb/`.

## Security baseline (phase 0, before phase 1)

Target: GitHub security guidance, read 2026-10-18:
[Quickstart for securing your repository](https://docs.github.com/en/code-security/getting-started/quickstart-for-securing-your-repository)
and [Security hardening for GitHub Actions](https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions).

Repo `antoine-voiry/saucedeck` is private on a personal account. Per the quickstart, CodeQL code
scanning, secret scanning/push protection and dependency review on private repos need an org on
Team/Enterprise. Those rows get a local substitute.

### GitHub repository controls

| GitHub recommendation | Available here | Action |
| --- | --- | --- |
| Restrict visibility/access | Yes | Keep private; review collaborators |
| Dependency graph + Dependabot alerts | Yes | **TODO (repo Settings, manual)**. Covers `github-actions` ecosystem only; PlatformIO is not a Dependabot ecosystem (unverified, check supported list) |
| Dependabot version updates | Yes | Done: `.github/dependabot.yml`, `github-actions`, weekly |
| Dependency review | No (private, personal) | Substitute: pinned versions in `platformio.ini`, reviewed in PR diff |
| CodeQL code scanning | No (private, personal) | Done: `pio check` (cppcheck) on `lib/deck_core` + `src` + `include`, fails on medium. If repo goes public, enable CodeQL default setup |
| Secret scanning + push protection | No (private, personal) | Done: `tools/hooks/pre-commit` + CI job (gitleaks 8.30.1, checksum verified). History clean (9 commits) |
| `SECURITY.md` policy | Yes | Done: `SECURITY.md` |
| Actions: pin third-party actions to full commit SHA | Yes | Done |
| Actions: `GITHUB_TOKEN` read-only by default | Yes | Done |
| Actions: no `pull_request_target`, no untrusted input in `run:` | Yes | Done |
| Actions: no self-hosted runners | Yes | Done |
| `CODEOWNERS` on `.github/workflows` | Yes | Done: `.github/` + `platformio.ini` |
| Branch protection / rulesets on `main` | Unverified for private personal repo on current plan | Check plan; if allowed, require CI green before merge |

CI workflow (`.github/workflows/ci.yml`): `pio test -e native` + `pio run -e saucedeck -e bringup`,
cache `~/.platformio`, warnings treated as failures on `deck_core`. Written, not yet run on GitHub.

### Firmware / build controls

| Item | Current state | Action |
| --- | --- | --- |
| Supply chain: platform | `stable` URL = moving target | Done 2026-10-18: pinned to 55.03.35 (installed, bench-verified build) |
| Supply chain: libs | none yet; Adafruit libs arrive in phase 6 | Pin exact versions in `lib_deps` (`@x.y.z`), commit lockfile-equivalent notes |
| Attack surface: radios | Wi-Fi/BT unused | Never init Wi-Fi/BT; check no `WiFi.h`/`BLE` linked (map file check in CI step) |
| Attack surface: USB | HID keyboard + CDC serial | Production CDC: no command interpreter, no debug shell. Bench commands stay in `bringup` env only |
| HID injection | Device types keys on host | Only fixed F13–F24 / consumer / one mute chord. No typed strings, no runtime-configurable macros without auth |
| Secrets | none | Done: gitleaks pre-commit + CI |
| Firmware integrity | plain flash, anyone with USB can reflash (1200 bps touch) | Decision needed: Secure Boot v2 + flash encryption. **eFuse burns are irreversible**, can brick dev loop, and must not touch `STRAP_JTAG_SEL`. Recommend: document risk, defer until hardware final |
| Build hygiene | `-Wall -Wextra` on native only | Done 2026-10-18: firmware `src/` gets `-Wall` (`-Wextra` trips a TinyUSB framework header), native `deck_core` gets `-Wall -Wextra -Werror`, cppcheck gate at medium |
| Input robustness | MCP read failure handled by retry | Tests: bad/garbage MCP reads never emit key events (fuzz-style unit test on `deckPressed` + debounce) |
| Vulnerability handling | none | Done: `SECURITY.md` |

## Hardware / host risks

1. **Backlight on GPIO9 unmeasured** (`docs/kb/peripherals.md`). Production keeps BL on
   permanently. Measure BL current ≤ 20 mA before phase 6, else move BL to 3V3.
2. **Volume is open-loop.** HID cannot read host volume. macOS VOL± step believed ≈ 6.25 %
   (16 steps) vs ≈ 2 % on Windows — unverified, measure on the Mac, then set the step constant.
   Sync chord (C6) is the only re-alignment: user turns host to 0 %, presses both knobs.
3. **Mic mute is open-loop too.** If the user mutes in the app, the indicator inverts.
   The Teams Mac shortcut (Cmd+Shift+M) is unverified.
4. **SPI redraw stalls polling.** Encoders are polled; a full icon blit at 20–40 MHz takes
   several ms (estimate, unverified). Partial redraw (phase 5) first, ISR (phase 7) if needed.
   Same class of issue as I2C stalls noted in `docs/kb/peripherals.md`.
5. **LCD init must match kb.** GMT020-02-8P verified sequence: COLMOD 0x55, MADCTL 0x00, INVON.
   Confirm the Adafruit `init(240, 320)` path gives correct colours; if not, send INVON / MADCTL
   after init.
6. **USB power budget 500 mA.** LCD BL + LEDs share it. LED frames capped at 250 mA estimate;
   check for brownout on the bench with the real LED count (70 µF bulk cap, `docs/kb/peripherals.md`).
