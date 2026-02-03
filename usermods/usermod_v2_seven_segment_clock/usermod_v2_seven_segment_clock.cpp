#include "wled.h"
#include "buzzer_manager.h"
// SevenSegmentClockUsermod
// Renders a 7‑segment clock overlay on the LED strip.
// - Supports 4 digits (HH:MM) or 6 digits (HH:MM:SS).
// - Flexible wiring via runtime segment order string (e.g., "cbafedg").
// - Geometry controls for pixels per segment/colon and base offset.
class SevenSegmentClockUsermod : public Usermod {
  bool enabled = true; // EnableClock in UI: master switch for overlay
  // Buzzer hourly chime configuration and manager
  BuzzerManager buzzer;
  BuzzerManager::Config buzCfg;
  bool buzzerEnabled = true;
  int8_t buzzerPin = 13;
  uint8_t buzzerType = 0; // 0=ACTIVE, 1=PASSIVE
  uint8_t quietStartHour = 21;
  uint8_t quietEndHour = 5;
  char soundProfile[16] = "alarm";
  bool halfHourChime = false;
  bool testNow = false;
  // Boot/toggle behaviors
  bool playOnBoot = true;
  char bootProfile[16] = "short";
  bool playOnToggle = true;
  char toggleOnProfile[16] = "short";
  char toggleOffProfile[16] = "short";
  bool lastOnState = true;
  // Per-event type overrides (0=ACTIVE,1=PASSIVE,-1=inherit)
  int8_t hourlyTypeOverride = -1;
  int8_t bootTypeOverride = -1;
  int8_t toggleOnTypeOverride = -1;
  int8_t toggleOffTypeOverride = -1;
  // Number of digits to render: 4 (HH:MM) or 6 (HH:MM:SS)
  uint8_t numDigits = 4;
  // LEDs per segment (A..G)
  uint8_t segPixels = 2;
  // LEDs per colon section
  uint8_t dotPixels = 2;
  // First LED index for the clock block on the strip
  uint16_t baseOffset = 0;
  // 12‑hour mode and leading zero handling
  bool useAmPm = true;
  bool hideLeadingZeroHours = true;
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
  // mark const to prefer flash storage where supported
  const byte digitsMatrix[12] = {
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
  /*
   * Cached geometry (CRITICAL)
   * - These values are precomputed to speed up rendering and MUST be recomputed
   *   whenever configuration values change (baseOffset, segPixels, dotPixels,
   *   numDigits, rightToLeft or order). Failure to recompute will cause incorrect
   *   digit placement (silent visual bugs) or out-of-bounds writes.
   *
   * Risks & symptoms if stale or incorrect:
   * - Digits display at wrong positions (looks shifted)
   * - Colons appear in wrong place or all LEDs stay on
   * - Out-of-bounds writes causing random LED colors, crashes, or heap corruption
   *
   * Mitigations:
   * - Call computeGeometry() after any config change (see readFromConfig & setup)
   * - Keep array bounds checks in digitStart() and clamp writes in writeDigit()
   * - If you edit geometry logic, re-run device tests: toggle enabled, change
   *   baseOffset/segPixels/dotPixels, switch 4<->6 digits, observe behavior.
   */
  // Cached geometry: precomputed once after config changes to avoid per-frame arithmetic
  uint16_t segGroupLen = 0;           // = segPixels * 7 (segments per digit + spacing)
  uint16_t digitIndices[6] = {0};     // Starting LED index for each digit (0-5)
  uint16_t colon1Idx = 0;             // First colon (HH:MM divider) starting index
  uint16_t colon2Idx = 0;             // Second colon (MM:SS divider) starting index, for 6-digit mode
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
  // CRITICAL: Precompute all geometry once. Call after any config change (setup, readFromConfig).
  // This eliminates repeated arithmetic in the hot render path.
  void computeGeometry() {
    segGroupLen = segPixels * 7;  // 7 segments per digit
    // Precompute starting LED index for each digit (0-5)
    for (uint8_t i = 0; i < 6; i++) {
      digitIndices[i] = baseOffset + (i * segGroupLen) + ((i / 2) * dotPixels);
    }
    // Colon positions: inserted after every 2 digits (HH | MM | SS)
    colon1Idx = baseOffset + (2 * segGroupLen);          // after HH
    colon2Idx = baseOffset + (4 * segGroupLen) + dotPixels;  // after MM
  }
  // Compute starting LED index for a digit, including colon spacing
  // Uses precomputed digitIndices array (set in computeGeometry) for O(1) lookup.
  // Bounds-checked to prevent array overflow.
  uint16_t digitStart(uint8_t index) {
    return (index < 6) ? digitIndices[index] : baseOffset;
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
        uint16_t end = offset + segPixels;
        // Clamp end to prevent out-of-bounds writes (safety critical for ESP8266 stability)
        if (end > strip.getLength()) end = strip.getLength();
        for (uint16_t j = offset; j < end; j++) {
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
    
    // first colon between hour and minute (uses precomputed colon1Idx for speed)
    for (uint8_t i = 0; i < dotPixels; i++) {
      uint16_t dot = colon1Idx + i;  // cached: no arithmetic needed
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
        uint16_t dot2 = colon2Idx + i;  // cached: no arithmetic needed
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
  void setup() override { 
    applyOrder(); 
    computeGeometry();  // CRITICAL: Must compute cached geometry before first render
    // Initialize buzzer with current configuration (defaults until readFromConfig runs on boot)
    buzCfg.enabled = buzzerEnabled;
    buzCfg.pin = buzzerPin;
    buzCfg.type = (buzzerType == 1) ? BuzzerManager::PASSIVE : BuzzerManager::ACTIVE;
    buzCfg.quietStartHour = quietStartHour;
    buzCfg.quietEndHour = quietEndHour;
    buzCfg.soundProfile = String(soundProfile);
    buzCfg.halfHourChime = halfHourChime;
    buzzer.begin(buzCfg);
    // Play on boot if enabled
    if (buzzerEnabled && playOnBoot) {
      buzzer.playNamed(String(bootProfile), bootTypeOverride);
    }
  }
  void loop() override {
    // Always check for chime regardless of overlay enabled state
    buzzer.maybeChime(hour(localTime), minute(localTime), second(localTime));
    buzzer.update(millis());
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
    // Ensure cached geometry is up-to-date before reporting diagnostics
    computeGeometry();
    // compute required LED length: cover last digit and any colon dots
    uint16_t lastDigitStart = digitStart((numDigits >= 1) ? (numDigits - 1) : 0);
    uint16_t needDigit = lastDigitStart + segGroupLen; // one past last used LED for digits
    uint16_t needColon1 = colon1Idx + dotPixels;
    uint16_t needColon2 = colon2Idx + dotPixels;
    uint16_t requiredLength = max(needDigit, max(needColon1, needColon2));
    bool geometryValid = (requiredLength <= strip.getLength());
    info["requiredLength"] = requiredLength;
    info["geometryValid"] = geometryValid;
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
    JsonObject clock = root.createNestedObject("SevenSegmentClock");
    clock["EnableClock"] = enabled;
    clock["numDigits"] = numDigits;
    clock["segPixels"] = segPixels;
    clock["dotPixels"] = dotPixels;
    clock["baseOffset"] = baseOffset;
    clock["useAmPm"] = useAmPm;
    clock["hideLeadingZeroHours"] = hideLeadingZeroHours;
    clock["hideLeadingZeroMinutes"] = hideLeadingZeroMinutes;
    clock["refreshMs"] = refreshMs;
    clock["order"] = order;
    clock["rightToLeft"] = rightToLeft; // default true: data flows right digit to left
    clock["showDots"] = showDots;
    clock["blinkDots"] = blinkDotsEnabled;
    JsonObject buz = root.createNestedObject("Buzzer");
    buz["enabled"] = buzzerEnabled;
    buz["pin"] = buzzerPin;
    buz["defaultType"] = buzzerType;
    buz["quietStartHour"] = quietStartHour;
    buz["quietEndHour"] = quietEndHour;
    buz["hourlyProfile"] = soundProfile;
    buz["hourlyType"] = hourlyTypeOverride;
    buz["halfHourChime"] = halfHourChime;
    buz["testNow"] = false;
    buz["playOnBoot"] = playOnBoot;
    buz["bootProfile"] = bootProfile;
    buz["bootType"] = bootTypeOverride;
    buz["playOnToggle"] = playOnToggle;
    buz["toggleOnProfile"] = toggleOnProfile;
    buz["toggleOnType"] = toggleOnTypeOverride;
    buz["toggleOffProfile"] = toggleOffProfile;
    buz["toggleOffType"] = toggleOffTypeOverride;
  }
  // Read configuration back from WLED web UI / JSON
  bool readFromConfig(JsonObject &root) override {
    JsonObject clock = root["SevenSegmentClock"];
    bool haveClock = !clock.isNull();
    if (!haveClock) return false;
    // EnableClock with backward-compatibility to "enabled"
    if (!getJsonValue(clock["EnableClock"], enabled)) {
      getJsonValue(clock["enabled"], enabled);
    }
    getJsonValue(clock["numDigits"], numDigits);
    getJsonValue(clock["segPixels"], segPixels);
    getJsonValue(clock["dotPixels"], dotPixels);
    getJsonValue(clock["baseOffset"], baseOffset);
    getJsonValue(clock["useAmPm"], useAmPm);
    getJsonValue(clock["hideLeadingZeroHours"], hideLeadingZeroHours);
    getJsonValue(clock["hideLeadingZeroMinutes"], hideLeadingZeroMinutes);
    getJsonValue(clock["refreshMs"], refreshMs);
    getJsonValue(clock["rightToLeft"], rightToLeft);
    getJsonValue(clock["showDots"], showDots);
    getJsonValue(clock["blinkDots"], blinkDotsEnabled);
    if (clock["order"].is<const char*>()) {
      const char* o = clock["order"].as<const char*>();
      if (o) { strncpy(order, o, sizeof(order)-1); order[sizeof(order)-1] = 0; applyOrder(); }
    }
    JsonObject buz = root["Buzzer"];
    // Read Buzzer section with fallback to previous flat fields
    if (!buz.isNull()) {
      getJsonValue(buz["enabled"], buzzerEnabled);
      getJsonValue(buz["pin"], buzzerPin);
      getJsonValue(buz["defaultType"], buzzerType);
      getJsonValue(buz["quietStartHour"], quietStartHour);
      getJsonValue(buz["quietEndHour"], quietEndHour);
      if (buz["hourlyProfile"].is<const char*>()) {
        const char* sp = buz["hourlyProfile"].as<const char*>();
        if (sp) { strncpy(soundProfile, sp, sizeof(soundProfile)-1); soundProfile[sizeof(soundProfile)-1] = 0; }
      }
      getJsonValue(buz["hourlyType"], hourlyTypeOverride);
      getJsonValue(buz["halfHourChime"], halfHourChime);
      getJsonValue(buz["testNow"], testNow);
      getJsonValue(buz["playOnBoot"], playOnBoot);
      if (buz["bootProfile"].is<const char*>()) {
        const char* bp = buz["bootProfile"].as<const char*>();
        if (bp) { strncpy(bootProfile, bp, sizeof(bootProfile)-1); bootProfile[sizeof(bootProfile)-1] = 0; }
      }
      getJsonValue(buz["bootType"], bootTypeOverride);
      getJsonValue(buz["playOnToggle"], playOnToggle);
      if (buz["toggleOnProfile"].is<const char*>()) {
        const char* tp = buz["toggleOnProfile"].as<const char*>();
        if (tp) { strncpy(toggleOnProfile, tp, sizeof(toggleOnProfile)-1); toggleOnProfile[sizeof(toggleOnProfile)-1] = 0; }
      }
      getJsonValue(buz["toggleOnType"], toggleOnTypeOverride);
      if (buz["toggleOffProfile"].is<const char*>()) {
        const char* tf = buz["toggleOffProfile"].as<const char*>();
        if (tf) { strncpy(toggleOffProfile, tf, sizeof(toggleOffProfile)-1); toggleOffProfile[sizeof(toggleOffProfile)-1] = 0; }
      }
      getJsonValue(buz["toggleOffType"], toggleOffTypeOverride);
    } else {
      // fallback to legacy
      getJsonValue(clock["buzzerEnabled"], buzzerEnabled);
      getJsonValue(clock["pin"], buzzerPin);
      getJsonValue(clock["buzzerType"], buzzerType);
      getJsonValue(clock["quietStartHour"], quietStartHour);
      getJsonValue(clock["quietEndHour"], quietEndHour);
      if (clock["soundProfile"].is<const char*>()) {
        const char* sp = clock["soundProfile"].as<const char*>();
        if (sp) { strncpy(soundProfile, sp, sizeof(soundProfile)-1); soundProfile[sizeof(soundProfile)-1] = 0; }
      }
      getJsonValue(clock["halfHourChime"], halfHourChime);
      getJsonValue(clock["testNow"], testNow);
      getJsonValue(clock["playOnBoot"], playOnBoot);
      if (clock["bootProfile"].is<const char*>()) {
        const char* bp = clock["bootProfile"].as<const char*>();
        if (bp) { strncpy(bootProfile, bp, sizeof(bootProfile)-1); bootProfile[sizeof(bootProfile)-1] = 0; }
      }
      getJsonValue(clock["playOnToggle"], playOnToggle);
      if (clock["toggleOnProfile"].is<const char*>()) {
        const char* tp = clock["toggleOnProfile"].as<const char*>();
        if (tp) { strncpy(toggleOnProfile, tp, sizeof(toggleOnProfile)-1); toggleOnProfile[sizeof(toggleOnProfile)-1] = 0; }
      }
      if (clock["toggleOffProfile"].is<const char*>()) {
        const char* tf = clock["toggleOffProfile"].as<const char*>();
        if (tf) { strncpy(toggleOffProfile, tf, sizeof(toggleOffProfile)-1); toggleOffProfile[sizeof(toggleOffProfile)-1] = 0; }
      }
    }
    // basic input validation
    if (quietStartHour > 23) quietStartHour = 23;
    if (quietEndHour > 23) quietEndHour = 7;
    if (buzzerType > 1) buzzerType = 0;
    if (buzzerPin < -1) buzzerPin = 13;
    if (numDigits != 4 && numDigits != 6) numDigits = 4;
    if (segPixels < 1) segPixels = 1;
    if (dotPixels < 1) dotPixels = 1;
    // CRITICAL: Recompute cached geometry after config changes to keep data in sync
    computeGeometry();
    // Apply buzzer configuration changes
    buzCfg.enabled = buzzerEnabled;
    buzCfg.pin = buzzerPin;
    buzCfg.type = (buzzerType == 1) ? BuzzerManager::PASSIVE : BuzzerManager::ACTIVE;
    buzCfg.quietStartHour = quietStartHour;
    buzCfg.quietEndHour = quietEndHour;
    buzCfg.soundProfile = String(soundProfile);
    buzCfg.halfHourChime = halfHourChime;
    buzzer.setConfig(buzCfg);
    // Execute test sound when explicitly requested by user (one-shot upon saving settings)
    if (testNow && buzzerEnabled) {
      buzzer.testPlay();
      testNow = false;
    }
    return true;
  }
  void appendConfigData() override {
    // Clock section
    oappend(F("addInfo('SevenSegmentClock:EnableClock',1,'enable clock overlay');"));
    // Buzzer section dropdowns
    oappend(F("dd=addDropdown('Buzzer','defaultType');"));
    oappend(F("addOption(dd,'Active (on/off)',0);"));
    oappend(F("addOption(dd,'Passive (tone/PWM)',1);"));
    // Dropdown for sound profile
    oappend(F("dd=addDropdown('Buzzer','hourlyProfile');"));
    oappend(F("addOption(dd,'short','short');"));
    oappend(F("addOption(dd,'double','double');"));
    oappend(F("addOption(dd,'triple','triple');"));
    oappend(F("addOption(dd,'long','long');"));
    oappend(F("addOption(dd,'gentle','gentle');"));
    oappend(F("addOption(dd,'rise','rise');"));
    oappend(F("addOption(dd,'fall','fall');"));
    oappend(F("addOption(dd,'alarm','alarm');"));
    oappend(F("addOption(dd,'pulse','pulse');"));
    oappend(F("addOption(dd,'bell','bell');"));
    oappend(F("addOption(dd,'plain 1s','plain1s');"));
    oappend(F("addOption(dd,'plain 2s','plain2s');"));
    oappend(F("addOption(dd,'plain 3s','plain3s');"));
    oappend(F("addOption(dd,'plain 4s','plain4s');"));
    oappend(F("addOption(dd,'plain 5s','plain5s');"));
    oappend(F("addOption(dd,'short+long 1s','shortLong1s');"));
    oappend(F("addOption(dd,'short+long 2s','shortLong2s');"));
    oappend(F("addOption(dd,'short+long 3s','shortLong3s');"));
    oappend(F("addOption(dd,'Mario motif','mario');"));
    oappend(F("addOption(dd,'Nokia tune','nokia');"));
    oappend(F("addOption(dd,'Hedwig motif','hedwig');"));
    oappend(F("addOption(dd,'Drone start','drone');"));
    // Info helpers
    oappend(F("addInfo('Buzzer:quietStartHour',1,'start hour (0-23) to mute chimes');"));
    oappend(F("addInfo('Buzzer:quietEndHour',1,'end hour (0-23), wrapping past midnight allowed');"));
    oappend(F("addInfo('Buzzer:testNow',1,'set true then save to audition current profile');"));
    // Boot/toggle selectors
    oappend(F("dd=addDropdown('Buzzer','bootProfile');"));
    oappend(F("addOption(dd,'short','short');"));
    oappend(F("addOption(dd,'double','double');"));
    oappend(F("addOption(dd,'triple','triple');"));
    oappend(F("addOption(dd,'long','long');"));
    oappend(F("addOption(dd,'gentle','gentle');"));
    oappend(F("addOption(dd,'rise','rise');"));
    oappend(F("addOption(dd,'fall','fall');"));
    oappend(F("addOption(dd,'alarm','alarm');"));
    oappend(F("addOption(dd,'pulse','pulse');"));
    oappend(F("addOption(dd,'bell','bell');"));
    oappend(F("addOption(dd,'Mario motif','mario');"));
    oappend(F("addOption(dd,'Nokia tune','nokia');"));
    oappend(F("addOption(dd,'Hedwig motif','hedwig');"));
    oappend(F("addOption(dd,'Drone start','drone');"));
    oappend(F("dd=addDropdown('Buzzer','toggleOnProfile');"));
    oappend(F("addOption(dd,'short','short');"));
    oappend(F("addOption(dd,'double','double');"));
    oappend(F("addOption(dd,'triple','triple');"));
    oappend(F("addOption(dd,'long','long');"));
    oappend(F("addOption(dd,'gentle','gentle');"));
    oappend(F("addOption(dd,'rise','rise');"));
    oappend(F("addOption(dd,'fall','fall');"));
    oappend(F("addOption(dd,'alarm','alarm');"));
    oappend(F("addOption(dd,'pulse','pulse');"));
    oappend(F("addOption(dd,'bell','bell');"));
    oappend(F("addOption(dd,'Mario motif','mario');"));
    oappend(F("addOption(dd,'Nokia tune','nokia');"));
    oappend(F("addOption(dd,'Hedwig motif','hedwig');"));
    oappend(F("addOption(dd,'Drone start','drone');"));
    oappend(F("dd=addDropdown('Buzzer','toggleOffProfile');"));
    oappend(F("addOption(dd,'short','short');"));
    oappend(F("addOption(dd,'double','double');"));
    oappend(F("addOption(dd,'triple','triple');"));
    oappend(F("addOption(dd,'long','long');"));
    oappend(F("addOption(dd,'gentle','gentle');"));
    oappend(F("addOption(dd,'rise','rise');"));
    oappend(F("addOption(dd,'fall','fall');"));
    oappend(F("addOption(dd,'alarm','alarm');"));
    oappend(F("addOption(dd,'pulse','pulse');"));
    oappend(F("addOption(dd,'bell','bell');"));
    oappend(F("addOption(dd,'Mario motif','mario');"));
    oappend(F("addOption(dd,'Nokia tune','nokia');"));
    oappend(F("addOption(dd,'Hedwig motif','hedwig');"));
    oappend(F("addOption(dd,'Drone start','drone');"));
    // Per-event type overrides
    oappend(F("dd=addDropdown('Buzzer','hourlyType');"));
    oappend(F("addOption(dd,'Inherit',-1);"));
    oappend(F("addOption(dd,'Active',0);"));
    oappend(F("addOption(dd,'Passive',1);"));
    oappend(F("dd=addDropdown('Buzzer','bootType');"));
    oappend(F("addOption(dd,'Inherit',-1);"));
    oappend(F("addOption(dd,'Active',0);"));
    oappend(F("addOption(dd,'Passive',1);"));
    oappend(F("dd=addDropdown('Buzzer','toggleOnType');"));
    oappend(F("addOption(dd,'Inherit',-1);"));
    oappend(F("addOption(dd,'Active',0);"));
    oappend(F("addOption(dd,'Passive',1);"));
    oappend(F("dd=addDropdown('Buzzer','toggleOffType');"));
    oappend(F("addOption(dd,'Inherit',-1);"));
    oappend(F("addOption(dd,'Active',0);"));
    oappend(F("addOption(dd,'Passive',1);"));
  }
  void addToJsonState(JsonObject& root) override {
    if (!buzzerEnabled) return;
    JsonObject buz = root["Buzzer"];
    if (buz.isNull()) buz = root.createNestedObject("Buzzer");
    buz["play"] = ""; // set to a profile name to play immediately
    buz["type"] = -1; // optional override: -1 inherit, 0 active, 1 passive
  }
  void readFromJsonState(JsonObject& root) override {
    if (!buzzerEnabled) return;
    // Handle immediate play requests without saving config
    JsonObject buz = root["Buzzer"];
    if (!buz.isNull()) {
      if (buz["play"].is<const char*>()) {
        const char* prof = buz["play"].as<const char*>();
        int typ = -1;
        if (buz["type"].is<int>()) typ = buz["type"].as<int>();
        if (prof && prof[0] != 0) buzzer.playNamed(String(prof), typ);
      }
    }
    // Detect global on/off toggle to play corresponding sound
    if (playOnToggle && root["on"].is<bool>()) {
      bool nowOn = root["on"].as<bool>();
      if (nowOn != lastOnState) {
        lastOnState = nowOn;
        buzzer.playNamed(String(nowOn ? toggleOnProfile : toggleOffProfile), nowOn ? toggleOnTypeOverride : toggleOffTypeOverride);
      }
    }
  }
};
static SevenSegmentClockUsermod _usermod_seven_segment_clock;
REGISTER_USERMOD(_usermod_seven_segment_clock);
