#pragma once
/*
 * BuzzerManager
 * Adds a configurable hourly chime for SevenSegmentClockUsermod.
 * Supports active buzzers (digital on/off) and passive buzzers (PWM/tone).
 * Platform-aware: uses LEDC on ESP32 and tone()/digitalWrite on ESP8266.
 *
 * Configuration fields:
 * - enabled: master switch for chime feature
 * - pin: GPIO to drive buzzer (default: 13)
 * - type: ACTIVE (on/off) or PASSIVE (tone/pwm)
 * - quietStartHour / quietEndHour: local-time range to suppress chimes
 * - soundProfile: select pattern/frequency envelope
 * - halfHourChime: optional half-hour short chime
 *
 * Usage:
 * 1) Call begin(config) once after reading config.
 * 2) Call setConfig(newConfig) when settings change (updates pin and mode).
 * 3) Call maybeChime(hour, minute, second) periodically (e.g., in loop()).
 *    It will play a chime exactly at the top of the hour (and half-hour if enabled),
 *    respecting quiet hours and the enabled flag. Includes guard to avoid repeats.
 * 4) Call testPlay() to audition current sound profile immediately.
 */

#include <Arduino.h>

class BuzzerManager {
public:
  enum BuzzerType : uint8_t { ACTIVE = 0, PASSIVE = 1 };

  struct Config {
    bool enabled = false;
    int8_t pin = 13;
    BuzzerType type = ACTIVE;
    uint8_t quietStartHour = 23; // default 23:00 …
    uint8_t quietEndHour = 7;    // … to 07:59 quiet
    String soundProfile = "short"; // "short","double","gentle","bell"
    bool halfHourChime = false;
  };

private:
  Config cfg;
  bool initialized = false;
  int8_t activePin = -1;
  uint8_t lastChimedHour = 255;
  uint8_t lastChimedMinute = 255;
  // Non-blocking sequence scheduler (CRITICAL: never use delay() in WLED paths)
  struct Step {
    bool useTone;
    uint16_t freq;
    uint16_t durMs;
    uint16_t gapMs;
  };
  Step seq[24];
  uint8_t seqLen = 0;
  uint8_t seqIdx = 0;
  bool inAction = false;
  unsigned long actionEnd = 0;
  unsigned long gapEnd = 0;
  bool playing = false;
  int8_t overrideType = -1; // -1=no override, 0=ACTIVE, 1=PASSIVE

  // ESP32 LEDC setup for PASSIVE buzzer
#if defined(ARDUINO_ARCH_ESP32)
  int ledcChannel = 3;     // pick a channel unlikely to conflict
  int ledcTimerBit = 8;    // 8-bit resolution is enough for buzzer
  bool ledcAttached = false;
#endif

  bool inQuietHours(uint8_t hour) const {
    if (cfg.quietStartHour == cfg.quietEndHour) return false; // no quiet period
    if (cfg.quietStartHour < cfg.quietEndHour) {
      return hour >= cfg.quietStartHour && hour < cfg.quietEndHour;
    }
    // wraps past midnight
    return hour >= cfg.quietStartHour || hour < cfg.quietEndHour;
  }

  void setupPin() {
    if (cfg.pin < 0) return;
    activePin = cfg.pin;
    pinMode(activePin, OUTPUT);
    stop();
#if defined(ARDUINO_ARCH_ESP32)
    if (cfg.type == PASSIVE) {
      ledcSetup(ledcChannel, 2000 /* Hz */, ledcTimerBit);
      ledcAttachPin(activePin, ledcChannel);
      ledcAttached = true;
    } else {
      if (ledcAttached) {
        ledcDetachPin(activePin);
        ledcAttached = false;
      }
    }
#endif
  }

  void seqReset() {
    seqLen = 0; seqIdx = 0; inAction = false; playing = false; actionEnd = 0; gapEnd = 0;
  }
  void addActive(uint16_t durMs, uint16_t gapMs = 60) {
    if (seqLen >= 24) return;
    seq[seqLen++] = {false, 0, durMs, gapMs};
  }
  void addTone(uint16_t freq, uint16_t durMs, uint16_t gapMs = 60) {
    if (seqLen >= 24) return;
    seq[seqLen++] = {true, freq, durMs, gapMs};
  }
  BuzzerType effectiveType() const {
    if (overrideType == 0) return ACTIVE;
    if (overrideType == 1) return PASSIVE;
    return cfg.type;
  }
  void startSequence() {
    if (activePin < 0 || seqLen == 0) return;
    seqIdx = 0; inAction = false; playing = true; actionEnd = 0; gapEnd = 0;
  }
  void processUpdate(unsigned long now) {
    if (!playing || activePin < 0) return;
    if (!inAction) {
      if (seqIdx >= seqLen) { playing = false; toneStop(); return; }
      // start action
      BuzzerType et = effectiveType();
      if (seq[seqIdx].useTone && et == PASSIVE) {
        toneStart(seq[seqIdx].freq);
      } else {
        digitalWrite(activePin, HIGH);
      }
      inAction = true;
      actionEnd = now + seq[seqIdx].durMs;
    } else {
      if ((long)(now - actionEnd) >= 0) {
        // end action, start gap
        toneStop();
        inAction = false;
        gapEnd = now + seq[seqIdx].gapMs;
        seqIdx++;
      }
    }
    if (!inAction && playing && seqIdx <= seqLen) {
      if (seqIdx >= seqLen) { playing = false; return; }
      if ((long)(now - gapEnd) >= 0) {
        // ready for next step; will start on next update pass
      }
    }
  }

  void toneStart(uint16_t freq) {
    if (activePin < 0) return;
    if (effectiveType() == ACTIVE) {
      digitalWrite(activePin, HIGH);
      return;
    }
#if defined(ARDUINO_ARCH_ESP32)
    if (ledcAttached) {
      ledcWriteTone(ledcChannel, freq);
    }
#elif defined(ARDUINO_ARCH_ESP8266)
    ::tone(activePin, freq);
#else
    digitalWrite(activePin, HIGH);
#endif
  }

  void toneStop() {
    if (activePin < 0) return;
    if (effectiveType() == ACTIVE) {
      digitalWrite(activePin, LOW);
      return;
    }
#if defined(ARDUINO_ARCH_ESP32)
    if (ledcAttached) {
      ledcWriteTone(ledcChannel, 0);
    }
#elif defined(ARDUINO_ARCH_ESP8266)
    ::noTone(activePin);
#else
    digitalWrite(activePin, LOW);
#endif
  }

  void buildShort() { addActive(120, 60); }

  void buildDouble() { addActive(100,80); addActive(140,60); }

  void buildGentle() { addActive(80,60); addActive(80,60); }

  void buildBell() { addActive(250,60); }

  void buildTriple() { addActive(70,60); addActive(70,60); addActive(70,60); }

  void buildLong() { addActive(450,60); }

  void buildRise() { addActive(80,60); addActive(120,60); addActive(180,60); }

  void buildFall() { addActive(180,60); addActive(120,60); addActive(80,60); }

  void buildAlarm() { addActive(120,80); addActive(120,80); addActive(120,150); addActive(400,60); }

  void buildPulse() { for (int i=0;i<6;i++) addActive(60,40); }

  void buildPlain(uint16_t ms) { addActive(ms,60); }

  void buildShortThenLong(uint16_t shortMs, uint16_t longMs) { addActive(shortMs,120); addActive(longMs,60); }

  // Melody helpers (PASSIVE gets musical notes, ACTIVE gets rhythmic beeps)
  void note(uint16_t f, uint16_t ms) { addTone(f, ms, 40); }

  void playProfileMario() {
    // Short motif inspired by Mario Overworld (truncated)
    seqReset(); note(659,160); note(659,160); addTone(0,0,120);
    note(659,160); addTone(0,0,180);
    note(523,160); note(659,160); addTone(0,0,100);
    note(784,160); addTone(0,0,220);
    note(392,160);
  }

  void playProfileNokia() {
    // Short motif of Nokia tune (Grand Valse, truncated)
    seqReset(); note(1319,140); note(1175,140); note(988,140); note(740,200); addTone(0,0,80);
    note(831,140); note(988,140); note(1175,140); note(988,180);
  }

  void playProfileHedwig() {
    // Hedwig's Theme motif (very truncated)
    seqReset(); note(988,220); note(1319,220); note(1568,260); addTone(0,0,120);
    note(1319,220); note(988,220); note(784,260);
  }

  void playProfileDroneStart() {
    // Rising startup sweep
    seqReset(); note(300,200); note(500,200); note(800,200); note(1200,250);
  }

  void playCurrentProfile() {
    seqReset();
    if (cfg.soundProfile == "short") { buildShort(); }
    else if (cfg.soundProfile == "double") { buildDouble(); }
    else if (cfg.soundProfile == "triple") { buildTriple(); }
    else if (cfg.soundProfile == "long") { buildLong(); }
    else if (cfg.soundProfile == "gentle") { buildGentle(); }
    else if (cfg.soundProfile == "rise") { buildRise(); }
    else if (cfg.soundProfile == "fall") { buildFall(); }
    else if (cfg.soundProfile == "alarm") { buildAlarm(); }
    else if (cfg.soundProfile == "pulse") { buildPulse(); }
    else if (cfg.soundProfile == "plain1s") { buildPlain(1000); }
    else if (cfg.soundProfile == "plain2s") { buildPlain(2000); }
    else if (cfg.soundProfile == "plain3s") { buildPlain(3000); }
    else if (cfg.soundProfile == "plain4s") { buildPlain(4000); }
    else if (cfg.soundProfile == "plain5s") { buildPlain(5000); }
    else if (cfg.soundProfile == "shortLong1s") { buildShortThenLong(80,1000); }
    else if (cfg.soundProfile == "shortLong2s") { buildShortThenLong(80,2000); }
    else if (cfg.soundProfile == "shortLong3s") { buildShortThenLong(80,3000); }
    else if (cfg.soundProfile == "mario") { playProfileMario(); }
    else if (cfg.soundProfile == "nokia") { playProfileNokia(); }
    else if (cfg.soundProfile == "hedwig") { playProfileHedwig(); }
    else if (cfg.soundProfile == "drone") { playProfileDroneStart(); }
    else { buildBell(); }
    startSequence();
  }

public:
  void playNamed(const String& name, int typeOverride = -1) {
    if (activePin < 0) return;
    String prev = cfg.soundProfile;
    int8_t prevOverride = overrideType;
    overrideType = (typeOverride == 0 || typeOverride == 1) ? typeOverride : -1;
    cfg.soundProfile = name;
    playCurrentProfile();
    cfg.soundProfile = prev;
    overrideType = prevOverride;
  }

public:
  void begin(const Config& c) {
    cfg = c;
    setupPin();
    initialized = true;
    lastChimedHour = 255;
    lastChimedMinute = 255;
  }

  void setConfig(const Config& c) {
    bool pinChanged = (cfg.pin != c.pin) || (cfg.type != c.type);
    cfg = c;
    if (pinChanged) setupPin();
  }

  void stop() { toneStop(); }

  void testPlay() {
    if (!initialized || !cfg.enabled) return;
    playCurrentProfile();
  }

  void maybeChime(uint8_t hour, uint8_t minute, uint8_t second) {
    if (!initialized || !cfg.enabled) return;
    if (inQuietHours(hour)) return;

    bool topOfHour = (minute == 0 && second == 0);
    bool halfHour = (cfg.halfHourChime && minute == 30 && second == 0);

    if (topOfHour) {
      if (lastChimedHour != hour || lastChimedMinute != 0) {
        lastChimedHour = hour;
        lastChimedMinute = 0;
        playCurrentProfile();
      }
    } else if (halfHour) {
      if (lastChimedHour != hour || lastChimedMinute != 30) {
        lastChimedHour = hour;
        lastChimedMinute = 30;
        // half-hour uses a lighter sound
        seqReset();
        if (cfg.soundProfile == "double") buildShort();
        else buildGentle();
        startSequence();
      }
    }
  }

  void update(unsigned long nowMs) { processUpdate(nowMs); }
};

