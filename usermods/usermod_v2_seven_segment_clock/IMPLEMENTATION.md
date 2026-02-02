# Seven Segment Clock Usermod — Technical Notes and Rationale

This document records the design decisions, migration approach, and internal mechanics of the Seven Segment Clock usermod. It is intended for future maintainers and AI assistants to quickly understand what the code does, why it does it that way, and how to extend or port it.

## Goal
- Migrate a 7‑segment digital clock overlay from an older WLED (v0.12.0) implementation (`wled00/overlay.cpp`) to the modern v2 Usermod API in the latest WLED.
- Preserve effects/segments behavior underneath the overlay (clock should “punch out” segments that are off, without fighting the running effect colors).
- Make wiring and geometry configurable at runtime (segment order and pixel counts).
- Keep the code simple enough that a beginner can edit it confidently.

## Files Created/Modified
- Added usermod code: [`usermod_v2_seven_segment_clock.cpp`](file:///c:/Users/kashif/Downloads/VS%20Code%20repos%20and%20projects/WLED-main%20(1)/WLED-main/usermods/usermod_v2_seven_segment_clock/usermod_v2_seven_segment_clock.cpp)
- Added manifest: [`library.json`](file:///c:/Users/kashif/Downloads/VS%20Code%20repos%20and%20projects/WLED-main%20(1)/WLED-main/usermods/usermod_v2_seven_segment_clock/library.json)
- Added docs (user guide): [`README.md`](file:///c:/Users/kashif/Downloads/VS%20Code%20repos%20and%20projects/WLED-main%20(1)/WLED-main/usermods/usermod_v2_seven_segment_clock/README.md)
- Added this technical document: [`IMPLEMENTATION.md`](file:///c:/Users/kashif/Downloads/VS%20Code%20repos%20and%20projects/WLED-main%20(1)/WLED-main/usermods/usermod_v2_seven_segment_clock/IMPLEMENTATION.md)
- Modified build config: [`platformio.ini`](file:///c:/Users/kashif/Downloads/VS%20Code%20repos%20and%20projects/WLED-main%20(1)/WLED-main/platformio.ini) — add `custom_usermods = seven_segment_clock` under `[env:nodemcuv2]`

## Architecture Overview
- WLED core time path:
  - `handleTime()` runs in the main loop [wled.cpp], ticks once per second, and calls `updateLocalTime()` [ntp.cpp].
  - `localTime` carries current time; helpers like `hour(localTime)`/`minute(localTime)`/`second(localTime)` read it.
- Usermod v2 API:
  - `setup()` initializes mapping (order string → segment index map).
  - `loop()` throttles internal time checks (uses `refreshMs`); core still updates time per second.
  - `handleOverlayDraw()` renders the clock every frame of the LED service.
  - `addToConfig()`/`readFromConfig()` expose JSON config to Web UI and persist settings.

### Rendering Strategy
- We only turn OFF LEDs for segments that are not part of the current digit.
  - This preserves the running effect/segment colors for “on” segments (the effect provides the color).
  - Benefits: Overlay coexists with effects; no color conflicts; simpler code.
- Colon blinking:
  - Controlled by `showDots` (show/hide) and `blinkDots` (blink vs steady).
  - Blink: every second the colon LEDs are cleared (`second(localTime) % 2`).
  - Steady: colons remain ON (retain effect color). Hide: cleared every frame.
  - Two colons exist when `numDigits = 6`.

## Data Model and Mapping
- Standard 7‑segment bit order: `abcdefg` with `a=bit6 … g=bit0`.
  - `digitsMatrix` encodes the segments per digit using that bit order.
- Wiring order (runtime):
  - `order` is a 7‑letter string (e.g., `"cbafedg"`) that describes the physical LED block order on the strip for a single digit.
  - `applyOrder()` builds `mapIdx[]` so that logical segments `a..g` map to their physical block indices.
  - Net effect: User can adapt software to any digit wiring without code changes.
 - Direction:
   - `rightToLeft` controls digit placement direction. When `true` (default), data input/rendering is from the rightmost digit to the leftmost.
   - When `false`, rendering goes left to right.
   - `baseOffset` must point to the first digit in your chosen direction (rightmost if `rightToLeft=true`, leftmost if `false`).

### Pixel Addressing
- Each digit uses `7 * segPixels` LEDs, in the block order defined by `order`.
- `digitStart(index)` computes the first LED of a digit:
  - `baseOffset + index * (segPixels * 7) + (index / 2) * dotPixels`
  - `(index / 2) * dotPixels` automatically inserts colon spacing after the second and fourth digits.
- Within a digit, segment `s` maps to `digitStart(index) + mapIdx[s] * segPixels`.
 - Direction mapping:
   - Logical positions `0..(numDigits-1)` represent left‑to‑right order: HH:MM(:SS).
   - Physical index used for rendering is `rightToLeft ? (numDigits - 1 - pos) : pos`.
   - This ensures a single switch changes direction everywhere consistently.

## Configuration Surface
- `enabled` — overlay on/off
- `numDigits` — `4` or `6`
- `segPixels` — LEDs per segment (typical `2`)
- `dotPixels` — LEDs per colon (typical `2`)
- `baseOffset` — LED index to start the clock at
- `useAmPm` — 12‑hour mode with AM/PM
- `hideLeadingZeroHours` — hide leading zero in HH
- `hideLeadingZeroMinutes` — hide leading zero in MM
- `refreshMs` — usermod’s internal time throttling; does not slow the LED frame rate
- `order` — 7‑letter runtime mapping string (`abcdefg` baseline)

Input validation:
- `numDigits` clamped to {4,6}
- `segPixels >= 1`, `dotPixels >= 1`
- `order` parsed only if present and treated as ASCII; mapping rebuilt immediately

## Migration Notes (v0.12 overlay.cpp → v2 Usermod)
- Old overlay path drew directly in `overlay.cpp`, controlled by `overlayCurrent` and `handleOverlays()`.
- New usermod uses `handleOverlayDraw()` to render over the current segment/effect frame.
- Time handling moved from local overlay routine to core `handleTime()`; we call `updateLocalTime()` in `loop()` as a safety.
- Right‑to‑left wiring and specific segment pairs were supported by introducing the `order` string and `mapIdx[]` mapping.
- Effects/segments “not working” in the old modified version were addressed by only clearing OFF segments; ON segments keep effect colors.

## Rationale and Trade‑Offs
- Simplicity over custom color management:
  - We do not set the color for “on” segments; the running effect handles it.
  - Alternatives (fixed color or per‑segment color) add UI and RAM complexity on ESP8266.
- `refreshMs` behavior:
  - WLED already updates time each second; `refreshMs` throttles usermod time checks and is mostly a no‑op visually unless rendering is gated by it.
  - If desired, gate digit writes on `refreshMs` to make seconds “step” by chosen cadence; currently left in sync with 1‑second ticks.
- ArduinoJson robustness:
  - Avoid parsing into fixed char arrays directly (e.g., `char[40]`); use `const char*` and `strncpy` with bounds checks.
  - Keeps ESP8266 memory usage safe and prevents template mismatch errors.
- Gamma/color decisions:
  - Removed `gamma32` color usage for overlay: avoids confusion and unused code since effects provide colors.

## Portability
- Modern WLED (0.13+) provides the v2 Usermod API used here; porting across minor releases is typically trivial.
- ESP8266 and ESP32 both supported; memory headroom is tighter on ESP8266, so keep config surface minimal.
- Very old WLED (0.12) used `overlay.cpp`; logic is portable but integration differs. Prefer upgrading to a current WLED if possible.

## Testing and Verification
- Geometry:
  - `numDigits = 4` and `6` both render correctly; ensure LED count covers all segments + colons.
  - Adjust `segPixels`, `dotPixels`, and `baseOffset` to match hardware.
  - Validate that colons blink each second (`second(localTime) % 2` toggling).
- Wiring:
  - Start with `order = "abcdefg"`; if digits look incorrect, set to your physical block sequence, e.g., `"cbafedg"`.
  - Confirm 2↔5 and 3↔E mix‑ups disappear after bit order correction (`a=bit6 … g=bit0`) and proper `order`.
- Build:
  - `custom_usermods = seven_segment_clock` added to your PlatformIO env.
  - `pio run -e nodemcuv2` compiles; `.bin` placed in `build_output/release/`.

## Extension Ideas
- Gate rendering on `refreshMs` to make seconds advance in steps (2s, 5s, etc.).
- Per‑segment color overlay (with UI): define a color for each segment and render ON segments explicitly.
- Add date display or alternate modes (cronixie style, text date for certain times).
- Animation hooks: briefly animate a digit change (fade/slide) within overlay.

## AI Handoff Checklist
- Understand that segments use `abcdefg` → `mapIdx[]` built from `order`.
- Bit ordering is fixed: `a=bit6 … g=bit0` in `digitsMatrix`.
- Overlay only clears OFF segments; it does not set the ON color.
- Colon positions depend on `numDigits` and `dotPixels`; spacing handled by `digitStart(index)`.
- Config keys and validation:
  - Clamp `numDigits` to 4 or 6; ensure `segPixels,dotPixels >= 1`.
  - Read `order` safely and rebuild mapping after changes.
- When porting:
  - Use v2 Usermod API (`setup`, `loop`, `handleOverlayDraw`, `addToConfig`, `readFromConfig`).
  - Add to `platformio.ini` → `custom_usermods = seven_segment_clock`.
  - Keep code minimal for ESP8266; avoid heavy JSON structures or per‑segment state in RAM.
