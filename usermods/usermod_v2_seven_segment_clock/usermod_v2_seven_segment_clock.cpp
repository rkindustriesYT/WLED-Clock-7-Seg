#include "wled.h"
// SevenSegmentClockUsermod
// Renders a 7‑segment clock overlay on the LED strip.
// - Supports 4 digits (HH:MM) or 6 digits (HH:MM:SS).
// - Flexible wiring via runtime segment order string (e.g., "cbafedg").
// - Geometry controls for pixels per segment/colon and base offset.
class SevenSegmentClockUsermod : public Usermod {
  bool enabled = true;
  // Number of digits to render: 4 (HH:MM) or 6 (HH:MM:SS)
  uint8_t numDigits = 4;
  // LEDs per segment (A..G)
  uint8_t segPixels = 2;
  // LEDs per colon section
  uint8_t dotPixels = 2;
  // First LED index for the clock block on the strip
  uint16_t baseOffset = 0;
  // 12‑hour mode and leading zero handling
  bool useAmPm = false;
  bool hideLeadingZeroHours = false;
  bool hideLeadingZeroMinutes = false;
  // Direction: by default data input and digit indexing go right-to-left.
  // Set to false to render left-to-right. Also ensure baseOffset points to the first digit in your chosen direction.
  bool rightToLeft = true;
  // Internal refresh throttle for time checks
  unsigned long lastUpdate = 0;
  uint16_t refreshMs = 500;
  // Dots (colons) control: show or hide, and blink behavior
  bool showDots = true;
  bool blinkDotsEnabled = true;
  // 7‑segment digit patterns using standard abcdefg bit order: a=bit6 … g=bit0
  byte digitsMatrix[12] = {
    //abcdefg
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
  };
  // Runtime segment order string. Change to match your physical segment block order.
  // Example: "cbafedg" if your digit blocks are wired C,B,A,F,E,D,G from left to right.
  char order[8] = "cbafedg";
  // mapIdx translates standard letter positions (a..g) to physical block indices
  uint8_t mapIdx[7] = {0,1,2,3,4,5,6};
  // Cache for blink state to avoid repeated second() calls
  bool lastBlinkState = false;
  uint8_t lastSecond = 255;
  void applyOrder() {
    // Direct mapping: translate letter position to order index
    for (int s = 0; s < 7; s++) {
      char letter = 'a' + s;
      mapIdx[s] = 0;
      for (int i = 0; i < 7; i++) {
        if (order[i] == letter) {
          mapIdx[s] = i;
          break;
        }
      }
    }
  }
  // Compute starting LED index for a digit, including colon spacing
  uint16_t digitStart(uint8_t index) {
    return baseOffset + (index * (segPixels * 7)) + ((index / 2) * dotPixels);
  }
  // Render a single digit: lights OFF for segments not used by the digit
  // Lighting ON color comes from the running WLED effect; we only clear segments that are off
  void writeDigit(uint8_t index, int val) {
    byte digit = (val < 0) ? 0 : digitsMatrix[val];
    uint16_t digitBase = digitStart(index);
    for (int s = 0; s < 7; s++) {
      uint16_t offset = digitBase + (mapIdx[s] * segPixels);
      bool on = ((digit >> (6 - s)) & 0x01);
      if (!on) {
        for (uint16_t j = offset; j < offset + segPixels; j++) {
          strip.setPixelColor(j, 0x000000);
        }
      }
    }
  }
  // Control colons: hide, blink, or steady (leave ON)
  void blinkDots() {
    // update blink state only when second changes (cached to avoid redundant calls)
    uint8_t curSecond = second(localTime);
    if (curSecond != lastSecond) {
      lastSecond = curSecond;
      lastBlinkState = (curSecond % 2 == 0);
    }
    
    // first colon between hour and minute
    for (uint8_t i = 0; i < dotPixels; i++) {
      uint16_t dot = baseOffset + 2 * (segPixels * 7) + i;
      if (!showDots) {
        strip.setPixelColor(dot, 0x000000); // hide
      } else if (blinkDotsEnabled && lastBlinkState) {
        strip.setPixelColor(dot, 0x000000); // blink OFF
      }
      // else: steady ON (do nothing, keep effect color)
    }
    // optional second colon between minute and second (for 6 digits)
    if (numDigits == 6) {
      for (uint8_t i = 0; i < dotPixels; i++) {
        uint16_t dot2 = baseOffset + 4 * (segPixels * 7) + dotPixels + i;
        if (!showDots) {
          strip.setPixelColor(dot2, 0x000000); // hide
        } else if (blinkDotsEnabled && lastBlinkState) {
          strip.setPixelColor(dot2, 0x000000); // blink OFF
        }
        // else: steady ON
      }
    }
  }
public:
  // Initialize mapping based on the configured order
  void setup() override { applyOrder(); }
  void loop() override {
    if (!enabled) return;
    if (millis() - lastUpdate < refreshMs) return;
    lastUpdate = millis();
    // localTime is updated by WLED core; no need to call updateLocalTime() here
  }
  void handleOverlayDraw() override {
    if (!enabled) return;
    // Split current time into digits with optional AM/PM handling
    byte h = hour(localTime);
    if (useAmPm) {
      if (h > 12) h -= 12;
      else if (h == 0) h = 12;
    }
    byte ht = h / 10;
    byte ho = h % 10;
    byte m = minute(localTime);
    byte mt = m / 10;
    byte mo = m % 10;
    auto phys = [&](uint8_t pos) -> uint8_t {
      return rightToLeft ? (numDigits - 1 - pos) : pos;
    };
    if (numDigits == 6) {
      byte s = second(localTime);
      byte st = s / 10;
      byte so = s % 10;
      // positions 0..5 represent left-to-right logical placement: HH:MM:SS
      writeDigit(phys(0), (hideLeadingZeroHours && ht == 0) ? -1 : ht);
      writeDigit(phys(1), ho);
      writeDigit(phys(2), (hideLeadingZeroMinutes && mt == 0) ? -1 : mt);
      writeDigit(phys(3), mo);
      writeDigit(phys(4), st);
      writeDigit(phys(5), so);
    } else {
      // positions 0..3 represent left-to-right logical placement: HH:MM
      writeDigit(phys(0), (hideLeadingZeroHours && ht == 0) ? -1 : ht);
      writeDigit(phys(1), ho);
      writeDigit(phys(2), (hideLeadingZeroMinutes && mt == 0) ? -1 : mt);
      writeDigit(phys(3), mo);
    }
    blinkDots();
  }
  void addToJsonInfo(JsonObject& root) override {
    JsonObject user = root["u"];
    if (user.isNull()) user = root.createNestedObject("u");
    JsonObject info = user.createNestedObject("SevenSegmentClock");
    char buf[9];
    byte h = hour(localTime);
    byte m = minute(localTime);
    if (useAmPm) {
      bool pm = h >= 12;
      if (h > 12) h -= 12;
      else if (h == 0) h = 12;
      snprintf(buf, sizeof(buf), "%02u:%02u", h, m);
      info["time"] = buf;
      info["ampm"] = pm ? "PM" : "AM";
    } else {
      snprintf(buf, sizeof(buf), "%02u:%02u", h, m);
      info["time"] = buf;
    }
  }
  // Expose usermod configuration to WLED web UI / JSON
  void addToConfig(JsonObject &root) override {
    JsonObject top = root.createNestedObject("SevenSegmentClock");
    top["enabled"] = enabled;
    top["numDigits"] = numDigits;
    top["segPixels"] = segPixels;
    top["dotPixels"] = dotPixels;
    top["baseOffset"] = baseOffset;
    top["useAmPm"] = useAmPm;
    top["hideLeadingZeroHours"] = hideLeadingZeroHours;
    top["hideLeadingZeroMinutes"] = hideLeadingZeroMinutes;
    top["refreshMs"] = refreshMs;
    top["order"] = order;
    top["rightToLeft"] = rightToLeft; // default true: data flows right digit to left
    top["showDots"] = showDots;
    top["blinkDots"] = blinkDotsEnabled;
  }
  // Read configuration back from WLED web UI / JSON
  bool readFromConfig(JsonObject &root) override {
    JsonObject top = root["SevenSegmentClock"];
    if (top.isNull()) return false;
    getJsonValue(top["enabled"], enabled);
    getJsonValue(top["numDigits"], numDigits);
    getJsonValue(top["segPixels"], segPixels);
    getJsonValue(top["dotPixels"], dotPixels);
    getJsonValue(top["baseOffset"], baseOffset);
    getJsonValue(top["useAmPm"], useAmPm);
    getJsonValue(top["hideLeadingZeroHours"], hideLeadingZeroHours);
    getJsonValue(top["hideLeadingZeroMinutes"], hideLeadingZeroMinutes);
    getJsonValue(top["refreshMs"], refreshMs);
    getJsonValue(top["rightToLeft"], rightToLeft);
    getJsonValue(top["showDots"], showDots);
    getJsonValue(top["blinkDots"], blinkDotsEnabled);
    if (top["order"].is<const char*>()) {
      const char* o = top["order"].as<const char*>();
      if (o) { strncpy(order, o, sizeof(order)-1); order[sizeof(order)-1] = 0; applyOrder(); }
    }
    // basic input validation
    if (numDigits != 4 && numDigits != 6) numDigits = 4;
    if (segPixels < 1) segPixels = 1;
    if (dotPixels < 1) dotPixels = 1;
    return true;
  }
};
static SevenSegmentClockUsermod _usermod_seven_segment_clock;
REGISTER_USERMOD(_usermod_seven_segment_clock);
