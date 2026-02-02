# Seven Segment Clock Usermod (WLED)

This usermod renders a 7‑segment digital clock on a continuous LED strip. It supports flexible wiring, 4‑digit (HH:MM) and 6‑digit (HH:MM:SS) layouts, and runtime configuration through WLED’s web UI.

## Features
- Runtime segment order using letters `abcdefg` so you can match any wiring.
- Flexible geometry: pixels per segment (`segPixels`), pixels per colon (`dotPixels`), and base offset (`baseOffset`).
- 4 or 6 digits (`numDigits`): 6 includes seconds.
- Optional AM/PM and leading-zero behavior.
- Works as an overlay: effects and segments continue to run under the clock.

## File Locations Changed/Added
- Created: `usermods/usermod_v2_seven_segment_clock/usermod_v2_seven_segment_clock.cpp`
- Created: `usermods/usermod_v2_seven_segment_clock/README.md` (this file)
- Created: `usermods/usermod_v2_seven_segment_clock/library.json`
- Modified: `platformio.ini` → add `custom_usermods = seven_segment_clock` to `[env:nodemcuv2]`
- Deleted (cleanup): `platformio_override.ini` (if present and conflicting)

## Build and Upload (NodeMCU v2 / ESP8266)
1. Ensure this directory exists:
   `WLED-main/usermods/usermod_v2_seven_segment_clock`
2. In `platformio.ini`, inside `[env:nodemcuv2]`, add:
   ```
   custom_usermods = seven_segment_clock
   ```
3. Build:
   ```
   pio run -e nodemcuv2
   ```
4. Upload via USB:
   ```
   pio run -e nodemcuv2 -t upload
   ```
   or flash the built BIN from `build_output/release/`.

## Configuration (Web UI)
Open WLED → UI → Config → Usermods → “SevenSegmentClock”.

- `enabled`: turn the overlay on/off.
- `rightToLeft`: data input direction. `true` = right digit to left (default). `false` = left to right.
- `numDigits`: 4 or 6. Set 6 to include seconds.
- `segPixels`: number of LEDs per segment (typical 2).
- `dotPixels`: LEDs for each colon section (typical 2).
- `baseOffset`: LED index of the first digit’s first segment.
- `showDots`: show/hide colons. When false, colons are cleared every frame.
- `blinkDots`: blink colons each second. When false and `showDots=true`, colons stay steady ON (retain effect color).
- `useAmPm`: use 12‑hour format with AM/PM.
- `hideLeadingZeroHours`: hide leading zero in hours (e.g., 09 → 9).
- `hideLeadingZeroMinutes`: hide leading zero in minutes.
- `refreshMs`: internal check interval. WLED updates time every second; this mostly affects how often the usermod refreshes cached time.
- `order`: a 7‑letter string describing your wiring order. Standard order is `abcdefg`. Example for your wiring: `cbafedg`.

## Wiring and Segment Order
- Data input direction:
  - Default is right digit to left (`rightToLeft = true`). Set to `false` to render left to right.
  - Ensure `baseOffset` points to the first digit in the chosen direction.
- A 7‑segment digit has segments labeled `a` through `g`. This usermod treats the LED blocks per digit in the order `abcdefg`.
- If your physical wiring is different, set `order` to match the physical sequence of segment blocks on the strip.
  - Example: if your digit’s segments on the strip are ordered `c, b, a, f, e, d, g`, set `order = "cbafedg"`.
- The mapping function uses `order` to translate standard segment letters to your LED block positions.

## Geometry and Layout
- Digits are laid out consecutively: each uses `7 * segPixels` LEDs.
- Colons “:” use `dotPixels` and are placed between HH:MM and MM:SS (if `numDigits = 6`).
- `baseOffset` lets you start the clock at a specific LED index if the clock does not begin at pixel 0.

## Digit Matrix
The 7‑segment patterns follow the standard `abcdefg` convention:
```
0b1111110, // 0
0b0110000, // 1
0b1101101, // 2
0b1111001, // 3
0b0110011, // 4
0b1011011, // 5
0b1011111, // 6
0b1110000, // 7
0b1111111, // 8
0b1111011, // 9
0b1001110, // C
0b1000111, // F
```
Bits are interpreted as `a=bit6 … g=bit0`.

## Refresh Behavior
- WLED updates `localTime` once per second globally; the usermod renders on every frame.
- Changing `refreshMs` does not slow strip updates; it only controls how often the usermod checks time internally.
- If you want seconds to “step” at a custom cadence (e.g., every 2 seconds), gate digit rendering on `refreshMs`. The current code leaves seconds in sync with WLED’s 1‑second tick.

## Portability to Other WLED Versions
- Modern WLED (0.13+) uses the v2 Usermod API (`setup`, `loop`, `handleOverlayDraw`, config methods). This usermod is portable across current versions with minimal changes.
- To use on another version:
  1. Copy the `usermod_v2_seven_segment_clock` folder into `usermods/`.
  2. Add `custom_usermods = seven_segment_clock` to your target environment in `platformio.ini`.
  3. Build and upload.
- Very old WLED (e.g., 0.12) used `overlay.cpp` integration. The rendering logic is compatible, but the integration point differs. Prefer upgrading to a recent WLED for usermods.

## Tips
- Start with `segPixels = 2`, `dotPixels = 2`, `order = "abcdefg"`, `numDigits = 4`.
- If you change `rightToLeft`, verify `baseOffset` still points to the first digit in that direction.
- If segments appear swapped or digits look wrong, adjust `order` to match wiring.
- Use `baseOffset` if your clock starts mid‑strip.
- For 6 digits, ensure you have enough LEDs: `6 * 7 * segPixels + 2 * dotPixels`.

## Troubleshooting
- “Digits look scrambled”: Check `order`.
- “Digits appear reversed”: Check `rightToLeft` and `baseOffset`.
- “Missing LED blocks”: Verify `segPixels` and total LED count.
- “Colons don’t blink”: They toggle every second; ensure `dotPixels >= 1`.
- “Build fails”: Confirm the `custom_usermods` entry and that the folder name matches `seven_segment_clock`.
