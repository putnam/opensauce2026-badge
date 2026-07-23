/*
  OPEN SAUCE 2026 Custom Badge Firmware
  by Chris Putnam and Gerard Hudson
  (and GLM 5.2 -- we blew threw this in about 3-4 hours on Saturday night mid-event. some slop has been excised, but ymmv)

  ------------------------------------------------------
  Custom SAMD21 (MKR Zero-based) badge. Use "Arduino MKR Zero" for the board in the Arduino IDE.

    Libraries needed:
    - Adafruit LIS3DH
    - Adafruit Unified Sensor (LIS3DH dependency)
    - Adafruit FreeTouch

  CONFIRMED BADGE PIN MAP:
    Mouth/bank LEDs (L->R): D1, D0, D3, D2, D4   (direction-indicator animation / menu selector)
    Forehead LED:  D5
    Right eye LED: D6
    Left eye LED:  D7
    I2C (accelerometer @ 0x18, WHO_AM_I overridden to 0x11): D11 / D12
    Piezo:                  A0
    Touch pads (L->R):      A1, A2, A3, ADC_BATTERY, A4, A5
    Mic:                    A6
    Button:                 D14 (pulled high when pressed)
    Surface mount LED:      LED_BUILTIN

  SAO PINS:
    Note 1: Both SAOs have electrically connected GPIO pins.
    Note 2: Simple SAOs provided at Open Sauce wire GPIO2 to their LED(s) only.
    Note 3: On this board the SAO ports' GPIO2 is wired to D9 which has no PWM support.
    SAO pin 1 -> 3.3v
    SAO pin 2 -> gnd
    SAO pin 3 (sda) -> board sda (PA08)
    SAO pin 4 (scl) -> board scl (PA09)
    SAO pin 5 (gpio1) -> D8 
    SAO pin 6 (gpio2) -> D9

  TUNED THRESHOLDS:
    Mic:   dip (500 - analogRead) > 200
    Whack: any single accel axis > 25.0 m/s^2
    Touch: delta from baseline > 200
    Swipe: the 4 center pads must activate in order (L->R or R->L) within 500ms

  SYSTEM STATES:
    ATTRACT MODE (default):
      - LEDs flash in various patterns (choose a pattern by touching a tooth)
      - A single button tap enters the Menu.
    MENU:
      - Uses the 5 mouth/bank LEDs as a selector, one lit at a time.
      - Short tap: cycle to the next slot (0-4).
      - Long press (>=600ms): select the highlighted slot and launch it (uses eye/forehead LEDs to indicate hold length)
      - Slot 1: Bop it
      - Slot 2: Simon
      - Slot 3: Balloon
      - Slot 4: RoboTheremin
      - Slot 5: Drum Machine
      - In any game, hold the button for 3 seconds and release to return to menu.
      - You can clear high-scores by holding the button during power-up for a couple of seconds. You'll hear a tone.

  BOP-IT (by chris): 
    - An approximation of the original bop-it-like game included on the 
      original Open Sauce 2026 badge firmware, plus some extra features
      like increasing difficulty and music cues.
    - Doing the WRONG action (even if it's above its own threshold) is an
      instant fail, same as running out of time. The console prints what
      you actually did vs. what was asked for.
    - Pace ramps up over TIME (not per-correct-answer): every 20 seconds
      survived, the reaction window shrinks one level, from a slow start
      up through "Hard" and finally "Really Hard" mode.
    - Survive a full 20 seconds AT Really Hard mode and the badge plays a
      short victory tune with a chasing LED animation, then keeps going.
    - Pace resets to slow at the start of each new run (i.e. after a fail).
    - Background music (High-Low-Low-Low arpeggio) speeds up and rises in
      pitch alongside the difficulty tier.

  SIMON (by Gerard):
    - Standard Simon. Tracks your personal best via FlashStorage. Prints 
      high score in binary after each run. If you exceed your PB you'll 
      get a little celebration.
  
  BALLOON (by chris):
    - An advanced lung-function test. Likely to replace PFTs in the medical
      setting within a few years. The user simply needs to keep blowing 
      into the microphone until the balloon pops. The balloon has a decay
      function if the blowing pauses.

  ROBOTHEREMIN (by Gerard, after several abandoned synth/stylophone/theremin ideas by Chris):
    - A simple noisemaker toy. With the badge flat in your palm, the X axis
      controls pitch and the Y axis controls "vibrato".
  
  DRUM MACHINE (by chris):
    - A quick attempt at a drum machine. Too many pads and too many LEDs, so
      I tried to force it to four pads and LEDs. Pressing the button rotates
      among snare/kick/hihat. The piezo is only capable of so much, so it's 
      a little quiet.

  EASTER EGG - SANDSTORM (by gerard & chris): 
    - If you brush the robot's teeth smoothly for about 3-4 swipes back and
      forth, you may trigger Sandstorm mode where the main riff loops. 
      Press the button to exit back to menu/attract mode.


  NOTES / TODO:
    Some annoying things the clanker did: the global "exit" functionality where
    you hold the button for 3s in the main loop isn't really global. Since most
    of the games run in their own loop, control doesn't return into the main loop()
    function until the game is already closed. So there's duplicated logic in
    every game to detect the button hold which is just wasting space.

  MORE TODO / UNIMPLEMENTED IDEAS!
    1. Ocarina mode (blow into mic, use touch pads to set notes. Maybe use the button to change scales.)
    2. Some kind of rhythm game. Could carry a metronome tick and just do call/response and increase difficulty to extreme
    3. Persistence of vision playback
    4. A balance game of some kind
    5. Quantize robotheremin to fixed scales, use button to change scales (similar to ocarina idea)
 
*/

#include <Wire.h>
#include <Adafruit_LIS3DH.h>
#include <Adafruit_FreeTouch.h>
#include <FlashStorage.h>

// Pins (reversed/confirmed directly)
#define PIN_PIEZO         A0
#define PIN_BUTTON        14

#define PIN_MIC           A6

#define PIN_LED_FOREHEAD    5
#define PIN_LED_RIGHTEYE    6
#define PIN_LED_LEFTEYE       7

// Open-sauce provided SAOs use the SAO connector GPIO2, which is on D9
// Note pin D9 does not support PWM (only 0-8, 10, A3, and A4 do)
#define PIN_LED_SAO 9

// Mic calibration. Tested in noisy and quiet environment
#define MIC_RESTING_VAL 500
#define MIC_DIP_THRESHOLD 200

// IMU/Accelerometer thresholds for detecting a "whack" or motion in games, in meters/sec (any axis)
#define IMU_WHACK_THRESHOLD 25.0

// Touch sensitivity relative to baseline measurements
#define TOUCH_THRESHOLD 200
// A swipe is registered LTR or RTL if the center 4 pads are touched in sequential order before this window expires.
#define SWIPE_WINDOW_MS 500

// Mouth/bank LEDs, physical left-to-right order
const int PIN_BANK[5] = {1, 0, 3, 2, 4};

// (almost) Every LED on the badge, for effects that light up the whole thing at once.
const int ALL_LEDS[9] = {
  PIN_BANK[0], PIN_BANK[1], PIN_BANK[2], PIN_BANK[3], PIN_BANK[4],
  PIN_LED_FOREHEAD, PIN_LED_RIGHTEYE, PIN_LED_LEFTEYE, PIN_LED_SAO
};
const int NUM_ALL_LEDS = 9;

// "Clockwise around the face" order for attract mode's rotation light show:
// teeth right-to-left, then left eye, forehead, right eye, then repeat.
// SAO is skipped since dual SAOs on either side would ruin our amazing illusion.
const int ATTRACT_ROTATION_ORDER[8] = {
  PIN_BANK[4], PIN_BANK[3], PIN_BANK[2], PIN_BANK[1], PIN_BANK[0],
  PIN_LED_LEFTEYE, PIN_LED_FOREHEAD, PIN_LED_RIGHTEYE
};

// NUM_UNIQUE_PADS = 6 touch channels, true physical left-to-right order
// (pad1..pad6). This ordering is the one source of truth for pad position -
// every subroutine that reads a pad by physical position indexes into
// touchPads[] below (or references the same index numbers), rather than
// keeping its own separate translation table.
const int NUM_UNIQUE_PADS = 6;

// ==========================================================================

// Note frequencies for Sandstorm
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_D5  587
#define NOTE_E5  659

// And a few more for Da Funk
#define NOTE_Ab4 415
#define NOTE_Gb4 370
#define NOTE_Eb4 311
#define NOTE_Db4 277
#define NOTE_B3  247
#define NOTE_Bb3 233
#define NOTE_Ab3 208

// and finally the last lot for the ocarina
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523

// Menu navigation tick notes: C minor scale, first 5 degrees (C D Eb F G),
// one per menu slot left-to-right.
const int MENU_NOTE_FREQS[6] = {523, 587, 622, 698, 784, 880}; // C5 D5 Eb5 F5 G5

Adafruit_LIS3DH lis = Adafruit_LIS3DH();

// Note that teeth count (6) != teeth LED count (5) so some apps need to map carefully
Adafruit_FreeTouch touchPads[NUM_UNIQUE_PADS] = {
  Adafruit_FreeTouch(A1, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),          // pad1, index 0
  Adafruit_FreeTouch(A2, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),          // pad2, index 1
  Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),          // pad3, index 2
  Adafruit_FreeTouch(ADC_BATTERY, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE), // pad4/tooth4, index 3
  Adafruit_FreeTouch(A4, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),          // pad5, index 4
  Adafruit_FreeTouch(A5, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE),          // pad6, index 5
};
int touchBaseline[NUM_UNIQUE_PADS];
bool touchWasActive[NUM_UNIQUE_PADS] = {false, false, false, false, false, false};
unsigned long touchActivationTime[NUM_UNIQUE_PADS] = {0, 0, 0, 0, 0, 0};

bool touchIsActive(int pad_i) {
    int reading = touchPads[pad_i].measure();
    return reading > touchBaseline[pad_i] + TOUCH_THRESHOLD;
}

// ---- Button debounce state (raw hardware level, used by everything) ----
// Button is wired active-HIGH: idle reads LOW, pressing drives it HIGH.
bool buttonStableState = LOW;
bool buttonLastReading = LOW;
unsigned long buttonLastChangeTime = 0;
const unsigned long DEBOUNCE_MS = 75;
bool buttonArmed = true; // (game use) must see a release before a press counts as fresh input

// ---- Top-level system state machine ----
enum SystemState { STATE_ATTRACT, STATE_MENU, STATE_GAME };
SystemState systemState = STATE_ATTRACT;

// Attract mode: slow random LED flicker, silent piezo
unsigned long attractNextUpdateTime = 0;

// Attract mode easter egg: swipe back-and-forth across the teeth 3 full
// cycles (L-R, R-L, L-R, R-L, L-R, R-L = 6 alternating swipes) quickly to
// trigger Sandstorm. Reuses the same detectSlide() used by Bop-It, so each
// individual swipe is already held to the same ~500ms pace.
int attractSwipeComboCount = 0;
int attractLastSwipeDirection = 0; // 0 = none, 1 = L->R, 2 = R->L
unsigned long attractLastSwipeTime = 0;
const unsigned long ATTRACT_SWIPE_COMBO_GAP_MS = 1000; // max gap between swipes to keep the combo alive
const int ATTRACT_SWIPE_COMBO_TARGET = 6; // 3 full back-and-forth cycles

// Attract mode light show selection: tap a single tooth pad (not a swipe)
// to switch which pattern is playing. 0=default random flicker (also pad 1),
// 1=fast strobe, 2=solid on, 3=rotation, 4=synced blink, 5=scan+breathe.
// Persisted via FlashStorage - survives both power cycles and normal
// menu/game round-trips within a session; attractLightMode always mirrors
// attractModeSavedData.lightMode, which is the single source of truth.
typedef struct {
  boolean valid;
  int lightMode;
} AttractModeData;

FlashStorage(attract_mode_flash_store, AttractModeData);
AttractModeData attractModeSavedData;
bool attractModeFlashInitialized = false;

void attractModeInitFlashStorage() {
  if (attractModeFlashInitialized) return;
  attractModeFlashInitialized = true;
  attractModeSavedData = attract_mode_flash_store.read();
  if (attractModeSavedData.valid == false) {
    attractModeSavedData.lightMode = 0;
    attractModeSavedData.valid = true;
    attract_mode_flash_store.write(attractModeSavedData);
    Serial.println("[attract] FlashStorage initialized.");
  } else {
    Serial.print("[attract] Loaded saved light mode: ");
    Serial.println(attractModeSavedData.lightMode + 1);
  }
}

int attractLightMode = 0;
int attractRotationIndex = 0;   // used by mode 3
bool attractSyncBlinkOn = false; // used by mode 4
float attractScanPosition = 0.0;   // used by mode 5 - current scan peak position among the 5 teeth (0-4)
int attractScanDirection = 1;      // used by mode 5 - +1 or -1, current sweep direction
unsigned long attractScanLastTick = 0; // used by mode 5 - for computing real elapsed time between updates
bool attractPadWasActive[NUM_UNIQUE_PADS] = {false, false, false, false, false, false};

// Menu: 5 mouth/bank LEDs as a selector
int menuSelectedIndex = 0;
const unsigned long LONG_PRESS_MS = 750; // matches the 3x250ms hold-indicator sequence below
bool menuButtonWasPressed = false;
unsigned long menuButtonPressStartTime = 0;
unsigned long menuLastActivityTime = 0;
const unsigned long MENU_TIMEOUT_MS = 5000; // no input for this long -> back to Attract

// Hold-to-select indicator: while holding the button in Menu mode, cycle
// the 3 face LEDs (left eye, forehead, right eye) every 250ms to show a
// selection is being made.
const int MENU_HOLD_LEDS[3] = {PIN_LED_LEFTEYE, PIN_LED_FOREHEAD, PIN_LED_RIGHTEYE};
bool menuHoldIndicatorActive = false;
int menuHoldIndicatorStep = 0;
unsigned long menuHoldIndicatorNextTime = 0;
const unsigned long MENU_HOLD_INDICATOR_INTERVAL_MS = 250;

// Global "kill switch": hold the button 3s from ANY state to force back to
// Attract mode. Tracked independently of per-state button logic so it never
// adds latency to normal gameplay's instant button-press detection.
bool exitHoldButtonWasPressed = false;
unsigned long exitHoldStartTime = 0;
const unsigned long EXIT_HOLD_MS = 3000;
bool exitHoldTriggered = false;       // fire the kill switch only once per physical hold
bool suppressMenuTapAfterExit = false; // the release of THIS press shouldn't also open the menu
bool exitIndicatorLedOn = false; // tracks whether LED_BUILTIN is currently lit for "release now"

// ---- Game state ----
enum Prompt { P_BUTTON, P_MIC, P_SLIDE_LEFT, P_SLIDE_RIGHT, P_WHACK };
const char* promptNames[] = {"Button", "Microphone", "Slide Left", "Slide Right", "Whack"};

int score = 0;
// Persistent high score (survives power cycles) via FlashStorage - same
// approach as Simon's high score.
typedef struct {
  boolean valid;
  int highScore;
} BopItGameData;

FlashStorage(bopit_flash_store, BopItGameData);
BopItGameData bopitSavedData;
bool bopitFlashInitialized = false;

void bopitInitFlashStorage() {
  if (bopitFlashInitialized) return;
  bopitFlashInitialized = true;
  bopitSavedData = bopit_flash_store.read();
  if (bopitSavedData.valid == false) {
    bopitSavedData.highScore = 0;
    bopitSavedData.valid = true;
    bopit_flash_store.write(bopitSavedData);
    Serial.println("[bopit] FlashStorage initialized.");
  } else {
    Serial.print("[bopit] Loaded High Score: ");
    Serial.println(bopitSavedData.highScore);
  }
}
Prompt currentPrompt;
unsigned long promptStartTime;
unsigned long reactionWindowMs;
bool waitingForInput = false;

// ---- Pacing: time-based difficulty ramp, one level every 20 seconds ----
const unsigned long LEVEL_WINDOWS_MS[] = {3000, 2500, 2000, 1500, 1000, 800, 600};
const int NUM_LEVELS = 7;               // indices 0..6
const int HARD_LEVEL_INDEX = 5;         // 800ms  = "Hard mode"
const int REALLY_HARD_LEVEL_INDEX = 6;  // 600ms  = "Really Hard mode"
const unsigned long LEVEL_DURATION_MS = 20000;

unsigned long gameStartTime;
bool hasCelebratedThisRun = false;

// ---- Background music: C3/C4 tick-tock, faster + higher each tier ----
const int MUSIC_BASE_LOW_FREQ = 131;   // C3
const int MUSIC_BASE_HIGH_FREQ = 262;  // C4 (one octave up)
const unsigned long MUSIC_BASE_NOTE_MS = 350; // slow start, room to speed up across tiers
const unsigned long MUSIC_MIN_NOTE_MS = 60;   // floor so it doesn't become a blur

unsigned long musicNoteIntervalMs;
int musicLowFreq, musicHighFreq;
int musicPatternIndex = 0; // 0 = Low (C3), 1 = High (C4) - alternating tick-tock
unsigned long musicNextNoteTime = 0;
int lastMusicTierPlayed = 0; // tracks difficulty tier for the speed-up sting

// Brief "jam" interlude after a correct answer, before the next prompt fires.
// Music keeps playing, LEDs stay dark, no input is judged during this window.
bool interludeActive = false;
unsigned long interludeEndTime = 0;

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(PIN_BUTTON, INPUT_PULLDOWN); // internal pull-down: LOW at idle, HIGH when pressed
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(PIN_LED_FOREHEAD, OUTPUT);
  pinMode(PIN_LED_RIGHTEYE, OUTPUT);
  pinMode(PIN_LED_LEFTEYE, OUTPUT);
  pinMode(PIN_LED_SAO, OUTPUT);
  for (int i = 0; i < 5; i++) pinMode(PIN_BANK[i], OUTPUT);

  checkBootHoldForHighScoreClear();

  Wire.begin();
  if (!lis.begin(0x18, 0x11)) {
    Serial.println("Couldnt start LIS3DH - check wiring");
  } else {
    Serial.println("LIS3DH found!");
    lis.setRange(LIS3DH_RANGE_4_G);
  }

  for (int i = 0; i < NUM_UNIQUE_PADS; i++) {
    if (!touchPads[i].begin()) {
      Serial.print("Failed to begin qt on pad ");
      Serial.println(i);
    }
  }
  calibrateTouch();

  randomSeed(analogRead(PIN_MIC));

  enterAttractMode();
}

void loop() {
  updateButtonDebounce();

  // Global kill switch: works from any state, checked before anything else.
  bool rawPressed = isButtonPressed();
  if (rawPressed && !exitHoldButtonWasPressed) {
    exitHoldStartTime = millis();
    exitHoldTriggered = false; // fresh press - reset the once-per-hold guard
  }
  if (rawPressed && !exitHoldTriggered && systemState != STATE_ATTRACT &&
      millis() - exitHoldStartTime >= EXIT_HOLD_MS) {
    exitHoldTriggered = true;
    digitalWrite(LED_BUILTIN, HIGH); // "you've held long enough, release now"
    exitIndicatorLedOn = true;
    enterAttractMode();
    menuButtonWasPressed = true;      // button IS still physically held - reflect that truthfully
    suppressMenuTapAfterExit = true;  // ...but don't let ITS release open the menu
  }
  exitHoldButtonWasPressed = rawPressed;

  // Centralized cleanup: whichever exit-hold implementation turned the
  // indicator on (this one, or one of the per-subroutine ones below), turn
  // it off here the instant the button is actually released. Checked every
  // iteration regardless of state, so it works even after a subroutine has
  // already handed off to Attract mode while the button is still held.
  if (exitIndicatorLedOn && !rawPressed) {
    digitalWrite(LED_BUILTIN, LOW);
    exitIndicatorLedOn = false;
  }

  switch (systemState) {
    case STATE_ATTRACT:
      updateAttractMode();
      handleMenuNavButton();
      checkAttractLightModeSelection();
      checkAttractEasterEgg();
      break;

    case STATE_MENU:
      handleMenuNavButton();
      updateMenuHoldIndicator();
      if (millis() - menuLastActivityTime > MENU_TIMEOUT_MS) {
        Serial.println("Menu timed out - back to attract mode.");
        enterAttractMode();
      }
      break;

    case STATE_GAME:
      if (buttonStableState == LOW) buttonArmed = true; // saw a release - next press counts
      checkForCelebration();
      updateBackgroundMusic();
      if (waitingForInput) {
        checkForCorrectInput();
        if (millis() - promptStartTime > reactionWindowMs) {
          failRound("ran out of time");
        }
      } else if (interludeActive) {
        if (millis() >= interludeEndTime) {
          interludeActive = false;
          startNewRound();
        }
      }
      break;
  }
}

// ---------------- Button debounce (shared low-level) ----------------

void updateButtonDebounce() {
  bool reading = digitalRead(PIN_BUTTON);
  if (reading != buttonLastReading) {
    buttonLastChangeTime = millis();
    buttonLastReading = reading;
  }
  if (millis() - buttonLastChangeTime > DEBOUNCE_MS) {
    buttonStableState = reading;
  }
}

bool isButtonPressed() {
  return buttonStableState == HIGH; // confirmed: pressed = HIGH, idle = LOW
}

// ---------------- Attract mode ----------------

void enterAttractMode() {
  systemState = STATE_ATTRACT;
  noTone(PIN_PIEZO);
  // pinMode(OUTPUT) first, not just digitalWrite() - this is the one shared
  // re-entry point every exit path (including Bop-It's global kill switch)
  // funnels through, so it needs to robustly release any pin that might
  // still be held in PWM mode (e.g. from Attract mode 5's analogWrite use),
  // the same fix we already needed for enterMenu() and the mode-switch and
  // Sandstorm transitions - digitalWrite() alone isn't reliably enough.
  resetAllLedsToDigitalOutput();
  attractNextUpdateTime = millis();

  // Reset easter-egg combo + underlying swipe-tracking state so nothing
  // stale from a previous session/subroutine carries over.
  attractSwipeComboCount = 0;
  attractLastSwipeDirection = 0;
  attractLastSwipeTime = 0;
  for (int i = 0; i < NUM_UNIQUE_PADS; i++) {
    touchWasActive[i] = false;
    touchActivationTime[i] = 0;
    attractPadWasActive[i] = false;
  }

  // Restore the saved/preferred light show (loads from flash only on the
  // very first call ever; every subsequent call just reuses the in-RAM
  // value, which stays current via checkAttractLightModeSelection()).
  attractModeInitFlashStorage();
  attractLightMode = attractModeSavedData.lightMode;
  attractRotationIndex = 0;
  attractSyncBlinkOn = false;
  attractScanLastTick = millis(); // avoid a huge bogus dt on mode 6's first tick after a long absence

  Serial.print("[attract] entered, restored light mode -> ");
  Serial.println(attractLightMode + 1);
  Serial.println("Attract mode. Tap the button to open the menu.");
  Serial.println("Tap a tooth pad to switch the light show (1=default 2=strobe 3=solid 4=rotation 5=sync blink 6=scan+breathe).");
}

// Runs whichever light show is currently selected. No sound in any mode.
void updateAttractMode() {
  if (millis() < attractNextUpdateTime) return;

  switch (attractLightMode) {
    case 0: { // default: slow random flicker
      clearAllLeds();
      int numToLight = random(1, 4); // 1-3 LEDs at a time, keeps it sparse/slow-feeling
      for (int k = 0; k < numToLight; k++) {
        digitalWrite(ALL_LEDS[random(0, NUM_ALL_LEDS)], HIGH);
      }
      attractNextUpdateTime = millis() + random(300, 800); // slow, irregular pace
      break;
    }

    case 1: { // fast random strobe
      clearAllLeds();
      int numToLight = random(3, NUM_ALL_LEDS + 1);
      for (int k = 0; k < numToLight; k++) {
        digitalWrite(ALL_LEDS[random(0, NUM_ALL_LEDS)], HIGH);
      }
      attractNextUpdateTime = millis() + random(40, 90); // fast
      break;
    }

    case 2: { // all lights solid on
      for (int i = 0; i < NUM_ALL_LEDS; i++) digitalWrite(ALL_LEDS[i], HIGH);
      attractNextUpdateTime = millis() + 500; // nothing to animate, just idle-check periodically
      break;
    }

    case 3: { // rotation - one LED at a time, "clockwise" around the face
      clearAllLeds();
      digitalWrite(ATTRACT_ROTATION_ORDER[attractRotationIndex], HIGH);

      // Indices 0-4 are the teeth (fast pace); 5-7 are eye/forehead/eye,
      // which are physically farther apart so they get ~30% longer to sit.
      unsigned long stepDelay = (attractRotationIndex < 5) ? 70 : 91;

      attractRotationIndex = (attractRotationIndex + 1) % 8;
      attractNextUpdateTime = millis() + stepDelay;
      break;
    }

    case 4: { // all lights blinking together, 500ms on/off
      attractSyncBlinkOn = !attractSyncBlinkOn;
      for (int i = 0; i < NUM_ALL_LEDS; i++) digitalWrite(ALL_LEDS[i], attractSyncBlinkOn ? HIGH : LOW);
      attractNextUpdateTime = millis() + 500;
      break;
    }

    case 5: { // teeth scan left/right (smooth PWM sweep) + eyes/forehead slow synced breathe
      unsigned long now = millis();

      // --- Teeth: Cylon/KITT-style scanning sweep, soft PWM glow not hard on/off ---
      unsigned long dtMs = now - attractScanLastTick;
      attractScanLastTick = now;
      const float SCAN_SPEED = 4.0 / 1200.0; // full 0-4 sweep in ~1200ms one direction
      const float FALLOFF_WIDTH = 1.2;       // how many tooth-widths the glow spreads

      attractScanPosition += attractScanDirection * SCAN_SPEED * dtMs;
      if (attractScanPosition >= 4.0) { attractScanPosition = 4.0; attractScanDirection = -1; }
      if (attractScanPosition <= 0.0) { attractScanPosition = 0.0; attractScanDirection = 1; }

      for (int i = 0; i < 5; i++) {
        float dist = abs((float)i - attractScanPosition);
        float brightness = 1.0 - (dist / FALLOFF_WIDTH);
        if (brightness < 0) brightness = 0;
        if (i == 4) {
          // Tooth5 (D4) shares a hardware timer with tone() (confirmed: the
          // Arduino SAMD core permanently dedicates TC5 to tone(), and this
          // pin's PWM apparently rides the same peripheral) - smooth
          // analogWrite() is fundamentally unusable here in a sketch that
          // ever calls tone() anywhere, which every game does. Simple
          // on/off threshold instead - not smooth, but reliably correct.
          digitalWrite(PIN_BANK[4], (brightness > 0.5) ? HIGH : LOW);
        } else {
          analogWrite(PIN_BANK[i], (int)(brightness * 255));
        }
      }

      // --- Eyes + forehead + SAO: slow, synchronized (not independent) breathe ---
      const float BREATHE_PERIOD_MS = 3500.0;
      float facePhase = (float)(now % (unsigned long)BREATHE_PERIOD_MS) / BREATHE_PERIOD_MS;
      float faceBrightness = (sin(facePhase * 2.0 * PI) * 0.5) + 0.5;
      int facePwm = (int)(faceBrightness * 255);
      // Forehead (D5) has the same tone()/TC5 sharing issue as tooth5 - same
      // on/off threshold fallback, matching the teeth-scan sync.
      digitalWrite(PIN_LED_FOREHEAD, (faceBrightness > 0.5) ? HIGH : LOW);
      // The SAO LED is not capable of PWM. It's plumbed to D9 (no PWM support).
      digitalWrite(PIN_LED_SAO, (faceBrightness > 0.5) ? HIGH : LOW);
      analogWrite(PIN_LED_RIGHTEYE, facePwm);
      analogWrite(PIN_LED_LEFTEYE, facePwm);
      

      attractNextUpdateTime = millis() + 20; // frequent updates for smooth motion/fade
      break;
    }
  }
}

// Detects a single tap (not a swipe) on any of the 6 teeth pads and switches
// the attract-mode light show to match. Mode numbering follows true
// physical left-to-right pad order (tap pad1 for mode1, pad2 for mode2,
// etc.) automatically, since touchPads[] is now itself in that same order -
// no separate translation needed. Independent of the swipe-combo easter
// egg, which needs all 4 inner pads in a timed sequence - a lone tap never
// satisfies that, so the two don't interfere with each other.
void checkAttractLightModeSelection() {
  for (int i = 0; i < NUM_UNIQUE_PADS; i++) {
    bool active = touchIsActive(i);
    if (active && !attractPadWasActive[i]) {
      if (attractLightMode != i) {
        if (attractLightMode == 5) {
          // Leaving the PWM scan+breathe mode - pinMode(OUTPUT) forces the pin's
          // peripheral mux back to plain GPIO. (analogWrite(pin, 0) does NOT
          // do this - it still engages the PWM/timer peripheral at 0% duty,
          // which can silently override later digitalWrite() calls if this
          // core doesn't fully release it on its own - same root cause we
          // hit with the piezo/DAC earlier.)
          resetAllLedsToDigitalOutput();
        }
        attractLightMode = i;
        attractModeSavedData.lightMode = i;
        attract_mode_flash_store.write(attractModeSavedData); // persist the new preference immediately
        attractRotationIndex = 0;
        attractSyncBlinkOn = false;
        if (i == 5) {
          attractScanPosition = 0.0;
          attractScanDirection = 1;
          attractScanLastTick = millis();
        }
        attractNextUpdateTime = millis(); // refresh immediately under the new mode
        Serial.print("[attract] light mode -> ");
        Serial.println(i + 1);
      }
    }
    attractPadWasActive[i] = active;
  }
}

// Checks for the swipe-combo easter egg. Call every loop tick while in
// Attract mode. Triggers Sandstorm on 3 full back-and-forth swipe cycles.
void checkAttractEasterEgg() {
  int result = detectSlide(); // 0 = nothing yet, 1 = L->R, 2 = R->L
  if (result == 0) return;

  unsigned long now = millis();
  bool alternates = (attractLastSwipeDirection != 0 && result != attractLastSwipeDirection);
  bool withinGap = (now - attractLastSwipeTime) <= ATTRACT_SWIPE_COMBO_GAP_MS;

  if (attractSwipeComboCount == 0 || (alternates && withinGap)) {
    attractSwipeComboCount++;
  } else {
    attractSwipeComboCount = 1; // pattern broken - this swipe starts a fresh combo
  }
  attractLastSwipeDirection = result;
  attractLastSwipeTime = now;

  Serial.print("[easter egg] swipe combo: ");
  Serial.println(attractSwipeComboCount);

  if (attractSwipeComboCount >= ATTRACT_SWIPE_COMBO_TARGET) {
    attractSwipeComboCount = 0;
    attractLastSwipeDirection = 0;
    Serial.println("[easter egg] Sandstorm unlocked!");
    // Force every LED pin back to plain GPIO in case the scan+breathe mode (5)
    // was active - pinMode(OUTPUT) releases the pin from PWM; analogWrite(0)
    // would not (it still engages the timer peripheral at 0% duty).
    resetAllLedsToDigitalOutput();
    playSandstorm(); // blocks
  }
}

// ---------------- Menu ----------------

void enterMenu() {
  systemState = STATE_MENU;
  menuSelectedIndex = 0;
  // Force every LED pin back to plain GPIO first, in case Attract mode's
  // scan+breathe (mode 5) was active - pinMode(OUTPUT) releases the pin from
  // PWM; analogWrite(0) would not (it still engages the timer at 0% duty).
  resetAllLedsToDigitalOutput();
  updateMenuLeds();
  tone(PIN_PIEZO, MENU_NOTE_FREQS[menuSelectedIndex], 30); // play slot 0's note on entry too
  menuLastActivityTime = millis();
  Serial.println("Menu: short tap = next option, long press = select.");
  Serial.println("  0: Bop-It   1: Simon   2: Balloon   3: RoboTheremin   4: Drum Machine  5: Digital Ocarina");
}

void updateMenuLeds() {
  allBankOff();
  for(int i = 0; i < 6; i++) if((menuSelectedIndex + 1) & bit(i)) digitalWrite(PIN_BANK[i], HIGH);
}

// Handles button press/release timing for both ATTRACT and MENU states.
// (Separate from the game's own button-armed logic, which only runs in STATE_GAME.)
void handleMenuNavButton() {
  bool pressedNow = isButtonPressed();

  if (pressedNow && !menuButtonWasPressed) {
    menuButtonPressStartTime = millis(); // press started
    if (systemState == STATE_MENU) {
      menuLastActivityTime = millis();
      // Start the hold-to-select face LED indicator - starts dark, first
      // LED lights after 250ms (handled by updateMenuHoldIndicator)
      menuHoldIndicatorActive = true;
      menuHoldIndicatorStep = 0;
      digitalWrite(MENU_HOLD_LEDS[0], LOW);
      digitalWrite(MENU_HOLD_LEDS[1], LOW);
      digitalWrite(MENU_HOLD_LEDS[2], LOW);
      menuHoldIndicatorNextTime = millis() + MENU_HOLD_INDICATOR_INTERVAL_MS;
    }
  }

  if (!pressedNow && menuButtonWasPressed) {
    unsigned long heldMs = millis() - menuButtonPressStartTime;

    // Only clean up the hold-indicator's face LEDs if it was actually
    // running (i.e. we were in Menu) - doing this unconditionally would
    // clobber Attract mode 5's own synced use of these same LEDs whenever
    // the button gets pressed for an unrelated reason (like the exit hold).
    if (menuHoldIndicatorActive) {
      menuHoldIndicatorActive = false;
      digitalWrite(PIN_LED_LEFTEYE, LOW);
      digitalWrite(PIN_LED_FOREHEAD, LOW);
      digitalWrite(PIN_LED_RIGHTEYE, LOW);
    }

    if (suppressMenuTapAfterExit) {
      suppressMenuTapAfterExit = false; // this release belonged to the exit hold - ignore it
    } else if (systemState == STATE_ATTRACT) {
      enterMenu();
    } else if (systemState == STATE_MENU) {
      if (heldMs >= LONG_PRESS_MS) {
        selectMenuOption(menuSelectedIndex);
      } else {
        menuSelectedIndex = (menuSelectedIndex + 1) % 6;
        updateMenuLeds();
        tone(PIN_PIEZO, MENU_NOTE_FREQS[menuSelectedIndex], 30); // rises one note per slot
      }
    }
  }

  menuButtonWasPressed = pressedNow;
}

// While the button is held down in Menu mode, lights the 3 face LEDs one at
// a time - left eye, then forehead, then right eye - every 250ms, each one
// staying on (not cycling/looping). Once all three are lit (750ms total),
// it stops updating; release at that point selects the current entry.
// Call every loop tick while in STATE_MENU.
void updateMenuHoldIndicator() {
  if (!menuHoldIndicatorActive) return;
  if (menuHoldIndicatorStep >= 3) return; // all three lit already - nothing more to do
  if (millis() < menuHoldIndicatorNextTime) return;

  digitalWrite(MENU_HOLD_LEDS[menuHoldIndicatorStep], HIGH);
  menuHoldIndicatorStep++;
  menuHoldIndicatorNextTime = millis() + MENU_HOLD_INDICATOR_INTERVAL_MS;
}

void selectMenuOption(int idx) {
  tone(PIN_PIEZO, 1800, 80); // confirm chirp
  delay(100);
  noTone(PIN_PIEZO);

  if (idx == 0) {
    startBopItGame();
  } else if (idx == 1) {
    simonGame(); // loops until the 3s exit hold; forces Attract mode itself
  } else if (idx == 2) {
    balloonGame();
    if (systemState == STATE_MENU) { // didn't get kicked to attract via kill switch
      Serial.println("Back to menu.");
      updateMenuLeds();
    }
  } else if (idx == 3) {
    roboThereminGame(); // loops until the 3s exit hold; forces Attract mode itself
  } else if (idx == 4) {
    drumMachineGame(); // loops until the 3s exit hold; forces Attract mode itself
  } else if(idx == 5){
    digitalOcarina(); // loops until the 3s exit hold; forces Attract mode itself
  }
  else {
    Serial.print("Slot ");
    Serial.print(idx);
    Serial.println(" has no game yet.");
    // quick "not implemented" blink on that slot, then stay in the menu
    for (int b = 0; b < 3; b++) {
      digitalWrite(PIN_BANK[idx], LOW);
      delay(100);
      digitalWrite(PIN_BANK[idx], HIGH);
      delay(100);
    }
    updateMenuLeds();
  }
}

// ---------------- Pacing ----------------

int currentLevelIndex() {
  unsigned long elapsed = millis() - gameStartTime;
  long rawLevel = elapsed / LEVEL_DURATION_MS;
  if (rawLevel > REALLY_HARD_LEVEL_INDEX) rawLevel = REALLY_HARD_LEVEL_INDEX;
  return (int)rawLevel;
}

void checkForCelebration() {
  if (hasCelebratedThisRun) return;
  unsigned long elapsed = millis() - gameStartTime;
  // A full 20s block beyond entering Really Hard mode = NUM_LEVELS full blocks elapsed
  if (elapsed >= (unsigned long)NUM_LEVELS * LEVEL_DURATION_MS) {
    hasCelebratedThisRun = true;
    playVictorySequence();
    promptStartTime = millis(); // don't punish the player for the time the song took
  }
}

// ---------------- Game flow ----------------

void startBopItGame() {
  bopitInitFlashStorage();
  systemState = STATE_GAME;
  score = 0;
  Serial.println("Starting Bop-It!");
  Serial.println("Get ready!");
  gameStartTime = millis();
  hasCelebratedThisRun = false;
  lastMusicTierPlayed = 0;
  startNewRound();
}

// Sets musicNoteIntervalMs/musicLowFreq/musicHighFreq for the current
// difficulty tier. Shared by both the post-answer interlude and startNewRound().
void computeMusicTempo() {
  int tierForScaling = currentLevelIndex(); // 0..REALLY_HARD_LEVEL_INDEX
  float speedFactor = pow(0.9, tierForScaling);
  musicNoteIntervalMs = (unsigned long)(MUSIC_BASE_NOTE_MS * speedFactor);
  if (musicNoteIntervalMs < MUSIC_MIN_NOTE_MS) musicNoteIntervalMs = MUSIC_MIN_NOTE_MS;

  float pitchFactor = pow(2.0, tierForScaling / 12.0); // semitone = 2^(1/12)
  musicLowFreq = (int)(MUSIC_BASE_LOW_FREQ * pitchFactor);
  musicHighFreq = (int)(MUSIC_BASE_HIGH_FREQ * pitchFactor);
}

// "Speed up" sting played once whenever the difficulty tier advances.
// 4 blocks of 4 rapid identical 32nd-note-style beeps, each block a
// semitone higher than the last. Starting pitch and beep tempo both come
// from computeMusicTempo()'s CURRENT (already-updated, new-tier) values,
// so the sting reflects the difficulty level being entered.
void playSpeedUpSting() {
  unsigned long beepMs = (unsigned long)(musicNoteIntervalMs * 0.3); // slightly faster than before
  if (beepMs < 30) beepMs = 30;
  unsigned long soundMs = (unsigned long)(beepMs * 0.6); // staccato - tone cuts off early, leaving a gap

  clearAllLeds();
  for (int block = 0; block < 4; block++) {
    float freq = musicHighFreq * pow(2.0, block / 12.0); // each block one semitone higher
    for (int beep = 0; beep < 4; beep++) {
      // Flash lights randomly every beep
      int numToLight = random(1, NUM_ALL_LEDS + 1);
      for (int k = 0; k < numToLight; k++) {
        digitalWrite(ALL_LEDS[random(0, NUM_ALL_LEDS)], HIGH);
      }
      tone(PIN_PIEZO, (int)freq, soundMs);
      delay(beepMs); // full slot - the silence after soundMs is what makes it staccato
      clearAllLeds();
    }
  }
  noTone(PIN_PIEZO);
  // Delay a little before another prompt appears for disambiguation
  interludeActive = true;
  interludeEndTime = millis() + 1000;

}

void startNewRound() {
  reactionWindowMs = LEVEL_WINDOWS_MS[currentLevelIndex()];
  buttonArmed = false; // require a fresh release+press before this round's button counts

  // Background music: 10% faster and one semitone higher per DIFFICULTY TIER
  // (not per correct answer) - so it only ramps up alongside the actual pace.
  // Computed FIRST, before the prompt appears, so if the tier just advanced
  // the speed-up sting plays as a clean transition rather than eating into
  // the upcoming prompt's reaction window.
  int newTier = currentLevelIndex();
  computeMusicTempo();
  if (newTier > lastMusicTierPlayed) {
    playSpeedUpSting();
    lastMusicTierPlayed = newTier;
    // The speed up sting kicks off an interlude delay which will expire and run this fn again
    return;
  }
  clearAllLeds();

  currentPrompt = (Prompt)random(0, 5);
  Serial.print("Prompt: ");
  Serial.println(promptNames[currentPrompt]);

  switch (currentPrompt) {
    case P_BUTTON:
      digitalWrite(PIN_LED_RIGHTEYE, HIGH);
      break;
    case P_MIC:
      digitalWrite(PIN_LED_LEFTEYE, HIGH);
      break;
    case P_WHACK:
      digitalWrite(PIN_LED_FOREHEAD, HIGH);
      break;
    case P_SLIDE_LEFT:
      animateBank(true);
      break;
    case P_SLIDE_RIGHT:
      animateBank(false);
      break;
  }

  // Clear any in-progress swipe tracking so a stale partial swipe from the
  // previous round can't bleed into this one.
  for (int i = 0; i < NUM_UNIQUE_PADS; i++) touchActivationTime[i] = 0;

  promptStartTime = millis();
  waitingForInput = true;
}

// Returns which action the player just performed, or -1 if nothing yet.
// Checks ALL sensors every call (not just the one matching currentPrompt),
// since doing the WRONG thing is a fail condition in bop-it.
int detectPlayerAction() {
  bool btn = isButtonPressed() && buttonArmed;
  bool mic = checkMicTriggered();
  bool whack = checkWhackTriggered();
  int swipe = detectSlide(); // 0 = none, 1 = L->R, 2 = R->L

  if (btn) return P_BUTTON;
  if (mic) return P_MIC;
  if (whack) return P_WHACK;
  if (swipe == 1) return P_SLIDE_LEFT;
  if (swipe == 2) return P_SLIDE_RIGHT;
  return -1;
}

void checkForCorrectInput() {
  int detected = detectPlayerAction();
  if (detected == -1) return; // nothing happened yet, keep waiting

  if (detected == (int)currentPrompt) {
    correctAnswer();
  } else {
    char reason[48];
    snprintf(reason, sizeof(reason), "did %s instead", promptNames[detected]);
    failRound(reason);
  }
}

bool checkMicTriggered() {
  // Mic signal centers around ~500 at rest and dips down on loud sound.
  return (MIC_RESTING_VAL - analogRead(PIN_MIC)) > MIC_DIP_THRESHOLD;
}

bool checkWhackTriggered() {
  sensors_event_t event;
  lis.getEvent(&event);
  float ax = abs(event.acceleration.x);
  float ay = abs(event.acceleration.y);
  float az = abs(event.acceleration.z);
  bool triggered = (ax > IMU_WHACK_THRESHOLD || ay > IMU_WHACK_THRESHOLD || az > IMU_WHACK_THRESHOLD);
  if (triggered) {
    Serial.print("  [whack triggered] x=");
    Serial.print(ax, 2);
    Serial.print(" y=");
    Serial.print(ay, 2);
    Serial.print(" z=");
    Serial.println(az, 2);
  }
  return triggered;
}

void correctAnswer() {
  score++;
  Serial.print("Score: ");
  Serial.println(score);
  tone(PIN_PIEZO, 1397, 50); // F6
  delay(50);
  tone(PIN_PIEZO, 1865, 70); // Bb6
  delay(70);
  waitingForInput = false;
  clearAllLeds();

  // Pause gameplay for a bit before resuming round
  interludeActive = true;
  interludeEndTime = millis() + (musicNoteIntervalMs * 4) + 250; // one full bar
}

void failRound(const char* reason) {
  waitingForInput = false;
  playFailureSequence(); // 1.5s chromatic descent + sparkly LEDs, pauses the game
  Serial.print("Wrong! Expected: ");
  Serial.print(promptNames[currentPrompt]);
  Serial.print(" - ");
  Serial.println(reason);
  Serial.print("Game Over! Score: ");
  Serial.println(score);
  if (score > bopitSavedData.highScore) {
    bopitSavedData.highScore = score;
    bopit_flash_store.write(bopitSavedData);
    Serial.println("[bopit] New High Score!");
  }
  Serial.print("  Best: ");
  Serial.println(bopitSavedData.highScore);

  score = 0;
  gameStartTime = millis(); // pace resets to slow for the new run
  hasCelebratedThisRun = false;
  lastMusicTierPlayed = 0;
  delay(1500);
  Serial.println("Get ready!");
  startNewRound();
}

// ---------------- Background music ----------------

// Non-blocking C3/C4 tick-tock ("tick, tock, tick, tock..."). Plays while
// waiting for input AND during the brief post-answer interlude, so it
// naturally pauses only during the correct/wrong feedback beep itself.
void updateBackgroundMusic() {
  if (!waitingForInput && !interludeActive) return;
  if (millis() < musicNextNoteTime) return;

  int freq = (musicPatternIndex == 0) ? musicLowFreq : musicHighFreq; // C3 then C4
  unsigned long noteLength = (unsigned long)(musicNoteIntervalMs * 0.45); // staccato - tone cuts off well before the next beat
  tone(PIN_PIEZO, freq, noteLength);

  musicPatternIndex = (musicPatternIndex + 1) % 2;
  musicNextNoteTime += musicNoteIntervalMs;
}

// ---------------- Failure sequence ----------------

// Blocking, ~1.5s "game over" sting: a full descending chromatic scale
// (12 semitones) on the piezo while every LED on the badge flickers in a
// random sparkly pattern. Pauses the game entirely while it plays.
// Kept as its own function so it can be reused anywhere else a "you lost"
// moment is needed, not just the main failRound() path.
void playFailureSequence() {
  const int numSteps = 20;
  const unsigned long totalMs = 500; // fast - was 1500
  const unsigned long stepMs = totalMs / numSteps; // 25ms per step, smooth/quick
  const float startFreq = 1200.0; // higher start for more "ring"
  const int totalSemitones = 18; // 1.5 octaves - wider, more dramatic sweep

  for (int i = 0; i < numSteps; i++) {
    float freq = startFreq * pow(2.0, -(totalSemitones * i / (float)(numSteps - 1)) / 12.0);
    unsigned long playMs = (stepMs > 5) ? stepMs - 2 : stepMs;
    tone(PIN_PIEZO, (int)freq, playMs);

    // sparkly flicker: clear all, then randomly light a handful
    clearAllLeds();
    int numToLight = random(1, NUM_ALL_LEDS + 1);
    for (int k = 0; k < numToLight; k++) {
      digitalWrite(ALL_LEDS[random(0, NUM_ALL_LEDS)], HIGH);
    }

    delay(stepMs);
  }
  clearAllLeds();
  noTone(PIN_PIEZO);
}

// ---------------- Victory sequence ----------------

void playVictorySequence() {
  Serial.println("*** Survived Really Hard mode for 20s - nice! ***");
  int melody[] = {523, 659, 784, 1047, 880, 988, 1047, 1319};
  int numNotes = sizeof(melody) / sizeof(int);
  for (int i = 0; i < numNotes; i++) {
    allBankOff();
    digitalWrite(PIN_BANK[i % 5], HIGH);
    tone(PIN_PIEZO, melody[i], 180);
    delay(200);
  }
  allBankOff();
  noTone(PIN_PIEZO);
}

// ---------------- Slot 2: Sandstorm ----------------

// Blocking. Loops until you exit. Lights are timed to the music. 
// Yes I hand-wrote this from memory and somehow it was accurate on the
// first try. Yes, there is something wrong with me (--chris).
//
void playSandstorm() {
  int melody[] = {
    // Phrase 1
    NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4,
    NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4,
    NOTE_E5, NOTE_E5, NOTE_E5, NOTE_E5, NOTE_E5, NOTE_E5, NOTE_E5,
    NOTE_D5, NOTE_D5, NOTE_D5, NOTE_D5, NOTE_D5, NOTE_D5, NOTE_D5,
    NOTE_A4, NOTE_A4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4,
    NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4,
    NOTE_E5, NOTE_E5, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4,
    NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4, NOTE_B4,
    NOTE_E5, NOTE_E5
    // 56 notes
  };
  int noteDurations[] = {
    16, 16, 16, 16, 8,
    16, 16, 16, 16, 16, 16, 8,
    16, 16, 16, 16, 16, 16, 8,
    16, 16, 16, 16, 16, 16, 8,
    16, 16, 16, 16, 16, 16, 8,
    16, 16, 16, 16, 16, 16, 8,
    16, 16, 16, 16, 16, 16, 8,
    16, 16, 16, 16, 16, 16, 8,
    16, 16
  };

  int numNotes = sizeof(melody) / sizeof(melody[0]);

  while (true) { // loops indefinitely - only exits when the button is pressed
    for (int thisNote = 0; thisNote < numNotes; thisNote++) {
      // Any button press exits immediately back to attract mode - no hold needed
      updateButtonDebounce();
      if (isButtonPressed()) {
        noTone(PIN_PIEZO);
        exitAllLedsAndEnterAttract();
        return;
      }

      int noteDuration = 1500 / noteDurations[thisNote];
      int totalTime = noteDuration * 1.30;
      int ledOnTime = noteDuration / 2;
      int ledOffTime = totalTime - ledOnTime;

      if (noteDurations[thisNote] == 16) {
        // Sparkle effect: each of the 5 small LEDs gets a 50% chance to light
        for (int i = 0; i < 5; i++) {
          if (random(2) == 1) digitalWrite(PIN_BANK[i], HIGH);
        }
        // Each of the 3 large LEDs gets a 50% chance to light
        if (random(2) == 1) digitalWrite(PIN_LED_LEFTEYE, HIGH);
        if (random(2) == 1) digitalWrite(PIN_LED_RIGHTEYE, HIGH);
        if (random(2) == 1) digitalWrite(PIN_LED_FOREHEAD, HIGH);
        if (random(2) == 1) digitalWrite(PIN_LED_SAO, HIGH);

      } else {
        // Map pitch to the 5 small LEDs for the longer 8th notes
        int activeLed = map(melody[thisNote], NOTE_A4, NOTE_E5, 0, 4);
        activeLed = constrain(activeLed, 0, 4);
        digitalWrite(PIN_BANK[activeLed], HIGH);

        // Trigger the 3 large LEDs heavily on the 8th notes
        if (noteDurations[thisNote] == 8) {
          digitalWrite(PIN_LED_LEFTEYE, HIGH);
          digitalWrite(PIN_LED_RIGHTEYE, HIGH);
          digitalWrite(PIN_LED_FOREHEAD, HIGH);
          digitalWrite(PIN_LED_SAO, HIGH);
        }
      }

      tone(PIN_PIEZO, melody[thisNote], noteDuration);

      delay(ledOnTime);

      // Turn ALL LEDs OFF to guarantee the strobe gap
      clearAllLeds();

      delay(ledOffTime);

      noTone(PIN_PIEZO);
    }
  } // end while(true)
}

// Eh, Sandstorm was more fun, but I'm leaving it in.
void playDaFunk() {
  int melody[] = {
    NOTE_Ab4, NOTE_Gb4, NOTE_Ab4, NOTE_B4, NOTE_Eb4, NOTE_Db4, NOTE_Eb4,
    NOTE_Gb4, NOTE_B3, NOTE_Bb3, NOTE_B3, NOTE_Eb4, NOTE_Ab3, NOTE_Bb3,
    NOTE_B3
    // 15 notes
  };
  // I really should've used a different notation system and implemented rests
  // 0.8 = 5 beats
  float noteDurations[] = {
    1, 4, 4, 4, 0.8,
    4, 4, 4, 0.8,
    4, 4, 4, 0.8,
    2, 2
  };

  int numNotes = sizeof(melody) / sizeof(melody[0]);

  while (true) { // loops indefinitely - only exits when the button is pressed
    for (int thisNote = 0; thisNote < numNotes; thisNote++) {
      // Any button press exits immediately back to attract mode - no hold needed
      updateButtonDebounce();
      if (isButtonPressed()) {
        noTone(PIN_PIEZO);
        exitAllLedsAndEnterAttract();
        return;
      }

      int noteDuration = 1250 / noteDurations[thisNote];

      tone(PIN_PIEZO, melody[thisNote], noteDuration);
      delay(noteDuration);
      noTone(PIN_PIEZO);
    }
  } // end while(true)
}

// ---------------- Slot 2: Simon ----------------

typedef struct {
  boolean valid;
  int highScore;
} SimonGameData;

FlashStorage(simon_flash_store, SimonGameData);
SimonGameData simonSavedData;
bool simonFlashInitialized = false;

// Note Simon uses 4 pads (the center 4 large teeth)
// There are 5 leds above. Ignore the center one.

const int SIMON_TOUCH_INDEX[4] = {1, 2, 3, 4}; // touchPads[] indices: pad1, pad2, pad5, pad6
const int SIMON_LED_INDEX[4] = {0, 1, 3, 4};   // PIN_BANK[] indices: light1, light2, light4, light5
const int SIMON_BUTTON_TONES[4] = {261, 329, 392, 523};

const int SIMON_MAX_LEVEL = 100;
int simonSequence[SIMON_MAX_LEVEL];
int simonCurrentLevel = 0;
int simonInputStep = 0;

void simonInitFlashStorage() {
  if (simonFlashInitialized) return;
  simonFlashInitialized = true;
  simonSavedData = simon_flash_store.read();
  if (simonSavedData.valid == false) {
    simonSavedData.highScore = 0;
    simonSavedData.valid = true;
    simon_flash_store.write(simonSavedData);
    Serial.println("[simon] FlashStorage initialized.");
  } else {
    Serial.print("[simon] Loaded High Score: ");
    Serial.println(simonSavedData.highScore);
  }
}

// Shared exit-hold check for Simon's various waiting points. Returns true
// (and forces Attract mode) the instant the button has been held 3s.
bool simonCheckExitHold(unsigned long &holdStart, bool &wasPressed) {
  updateButtonDebounce();
  bool pressedNow = isButtonPressed();
  if (pressedNow && !wasPressed) holdStart = millis();
  bool exiting = pressedNow && (millis() - holdStart >= EXIT_HOLD_MS);
  wasPressed = pressedNow;
  if (exiting) {
    digitalWrite(LED_BUILTIN, HIGH); // "you've held long enough, release now"
    exitIndicatorLedOn = true;
    noTone(PIN_PIEZO);
    exitAllLedsAndEnterAttract();
  }
  return exiting;
}

void simonLightAndPlay(int buttonIndex, int duration) {
  digitalWrite(PIN_BANK[SIMON_LED_INDEX[buttonIndex]], HIGH);
  tone(PIN_PIEZO, SIMON_BUTTON_TONES[buttonIndex]);
  delay(duration);
  digitalWrite(PIN_BANK[SIMON_LED_INDEX[buttonIndex]], LOW);
  noTone(PIN_PIEZO);
}

// Returns true if the exit hold fired mid-sequence (caller should bail out)
bool simonPlaySequence(unsigned long &holdStart, bool &wasPressed) {
  Serial.print("[simon] Playing sequence level: ");
  Serial.println(simonCurrentLevel);
  delay(1000);

  int noteDuration = 400 - ((simonCurrentLevel / 5) * 50);
  int pauseDuration = 200 - ((simonCurrentLevel / 5) * 25);
  if (noteDuration < 150) noteDuration = 150;
  if (pauseDuration < 50) pauseDuration = 50;

  for (int i = 0; i <= simonCurrentLevel; i++) {
    if (simonCheckExitHold(holdStart, wasPressed)) return true;
    simonLightAndPlay(simonSequence[i], noteDuration);
    delay(pauseDuration);
  }
  return false;
}

int simonReadButtons() {
  for (int i = 1; i < 5; i++) {
    if (touchIsActive(i)) {
      return i;
    }
  }
  return -1;
}

// Returns true if the exit hold fired (caller should bail out)
bool simonGameOver(unsigned long &holdStart, bool &wasPressed) {
  Serial.print("[simon] Game Over. Final Score: ");
  Serial.println(simonCurrentLevel);

  if (simonCurrentLevel > simonSavedData.highScore && simonCurrentLevel > 0) {
    Serial.println("[simon] New High Score!");
    simonSavedData.highScore = simonCurrentLevel;
    simon_flash_store.write(simonSavedData);

    int highTones[] = {261, 329, 392, 523, 659, 523, 784};
    int highDelays[] = {100, 100, 100, 100, 300, 100, 400};

    for (int t = 0; t < 7; t++) {
      tone(PIN_PIEZO, highTones[t]);
      unsigned long noteStart = millis();
      while (millis() - noteStart < (unsigned long)highDelays[t]) {
        int randomLed = random(0, 8);
        if (randomLed < 5) {
          digitalWrite(PIN_BANK[randomLed], HIGH);
          delay(15);
          digitalWrite(PIN_BANK[randomLed], LOW);
        } else {
          int faceLed = (randomLed - 5 == 0) ? PIN_LED_FOREHEAD : (randomLed - 5 == 1) ? PIN_LED_RIGHTEYE : PIN_LED_LEFTEYE;
          digitalWrite(faceLed, HIGH);
          delay(15);
          digitalWrite(faceLed, LOW);
        }
      }
    }
    noTone(PIN_PIEZO);
  } else {
    tone(PIN_PIEZO, 300); delay(150);
    tone(PIN_PIEZO, 250); delay(150);
    tone(PIN_PIEZO, 200); delay(150);
    tone(PIN_PIEZO, 150); delay(400);
    noTone(PIN_PIEZO);
  }

  delay(500);

  Serial.print("[simon] Displaying Score in Binary: ");
  Serial.println(simonCurrentLevel);
  for (int i = 0; i < 5; i++) {
    digitalWrite(PIN_BANK[4 - i], ((simonCurrentLevel >> i) & 1) ? HIGH : LOW);
  }

  unsigned long scoreDisplayStart = millis();
  while (millis() - scoreDisplayStart < 3000) {
    if (simonCheckExitHold(holdStart, wasPressed)) return true;
    delay(10);
  }

  allBankOff();
  return false;
}

void simonGame() {
  simonInitFlashStorage();
  allBankOff(); // clear the leftover menu-selector LED before gameplay starts

  unsigned long holdStart = 0;
  bool wasPressed = false;

  while (true) {
    // --- start a fresh round ---
    simonCurrentLevel = 0;
    simonSequence[simonCurrentLevel] = random(0, 4);

    bool roundOver = false;
    while (!roundOver) {
      if (simonPlaySequence(holdStart, wasPressed)) return; // exited mid-sequence

      simonInputStep = 0;
      bool waitingForInput = true;
      unsigned long inputDeadline = millis() + 1000; // 1s timeout, starts right when Simon finishes prompting
      while (waitingForInput) {
        if (simonCheckExitHold(holdStart, wasPressed)) return;

        if (millis() > inputDeadline) {
          Serial.println("[simon] Input timeout.");
          roundOver = true;
          waitingForInput = false;
          break;
        }

        int pressedButton = simonReadButtons();
        if (pressedButton >= 1 && pressedButton <= 4) {
          Serial.print("[simon] Input detected on button: ");
          Serial.println(pressedButton);

          simonLightAndPlay(pressedButton - 1, 300);
          Serial.print("Expected button: ");
          Serial.println(simonSequence[simonInputStep] + 1);
          if (pressedButton == (simonSequence[simonInputStep] + 1)) {
            simonInputStep++;
            if (simonInputStep > simonCurrentLevel) {
              Serial.println("[simon] Level complete.");
              simonCurrentLevel++;
              simonSequence[simonCurrentLevel] = random(0, 4);
              delay(500);
              waitingForInput = false; // back to sequence playback for next level
            } else {
              inputDeadline = millis() + 1000; // reset timeout for the next press in this sequence
            }
          } else {
            Serial.println("[simon] Wrong input.");
            roundOver = true;
            waitingForInput = false;
          }

          while (simonReadButtons() != -1) {
            delay(10); // wait for release
          }
        }
      }
    }

    if (simonGameOver(holdStart, wasPressed)) return;
    // otherwise loop back around for a fresh round, same as Bop-It after a fail
  }
}

// ---------------- Boot-hold high score clear ----------------

// Hold the momentary switch through power-up for 2 full seconds to wipe
// both Bop-It's and Simon's persisted high scores back to 0. Checked once,
// very early in setup(), before anything else initializes. Plays the same
// descending tone sweep as the balloon's deflate intro as confirmation.
void clearAllHighScores() {
  bopitSavedData.highScore = 0;
  bopitSavedData.valid = true;
  bopit_flash_store.write(bopitSavedData);
  bopitFlashInitialized = true;

  simonSavedData.highScore = 0;
  simonSavedData.valid = true;
  simon_flash_store.write(simonSavedData);
  simonFlashInitialized = true;

  Serial.println("[boot] High scores cleared (Bop-It + Simon).");
}

void checkBootHoldForHighScoreClear() {
  if (digitalRead(PIN_BUTTON) != HIGH) return; // not held at boot - nothing to do

  Serial.println("[boot] Button held at power-up - checking for high-score clear...");
  unsigned long holdStart = millis();
  bool stillHeld = true;
  while (millis() - holdStart < 2000) {
    if (digitalRead(PIN_BUTTON) != HIGH) {
      stillHeld = false;
      break;
    }
    delay(10);
  }

  if (stillHeld) {
    clearAllHighScores();
    balloonIntroDeflate(); // reused as the "wipe" confirmation tone + LED sweep
  } else {
    Serial.println("[boot] Released early - high scores NOT cleared.");
  }
}

// ---------------- Slot 3: Balloon ----------------

// Blow continuously into the mic (brief gaps tolerated) to inflate a balloon
// over 10 seconds. Progress shown as a fill-bar across the 5 mouth LEDs, with
// a low tone that pitch-bends upward as you get closer to 100%. Reaching
// 10 continuous seconds pops it: a screech, then a celebration light show.
// If the blow breaks for too long, progress resets to 0 and you can retry -
// only a pop or the 3s kill switch actually ends the subroutine.
// Plays a downward tone sweep while draining the mouth LEDs from 5 to 0
// over ~1000ms - a "the balloon just deflated" intro beat.
void balloonIntroDeflate() {
  const unsigned long INTRO_MS = 1000;
  const int startFreq = 900;
  const int endFreq = 150;
  unsigned long start = millis();

  while (millis() - start < INTRO_MS) {
    float t = (float)(millis() - start) / (float)INTRO_MS;
    int freq = startFreq - (int)(t * (startFreq - endFreq));
    tone(PIN_PIEZO, freq);

    int litCount = 5 - (int)(t * 5.0);
    if (litCount > 5) litCount = 5;
    if (litCount < 0) litCount = 0;
    for (int i = 0; i < 5; i++) digitalWrite(PIN_BANK[i], (i < litCount) ? HIGH : LOW);

    delay(10);
  }
  noTone(PIN_PIEZO);
  allBankOff();
}

void balloonGame() {
  const unsigned long BALLOON_TARGET_MS = 10000; // 10s continuous (at 1x) = 100%
  const unsigned long BALLOON_GRACE_MS = 500;   // longer debounce - brief pauses don't decay at all
  const int MIC_BLOW_THRESHOLD = -200;          // sustained blow shows as dip <= -200 (ignore positive dips - noise)
  const float BALLOON_DECAY_PER_SEC = 0.20;     // lose 20% of capacity per second once decaying
  const float BALLOON_DECAY_MS_PER_MS = (BALLOON_DECAY_PER_SEC * BALLOON_TARGET_MS) / 1000.0;
  const float BALLOON_CHARGE_MULTIPLIER = 1.0;  // real-time 1:1 - full 7s of sustained blowing to pop

  unsigned long blowAccumulatedMs = 0;
  unsigned long lastBlowTime = 0;
  bool everBlew = false;
  unsigned long lastLoopTime = millis();
  unsigned long lastToneUpdate = 0;

  unsigned long holdStart = 0;
  bool wasPressed = false;
  bool lastBlowingState = false;
  bool isDecaying = false;
  unsigned long lastStatusPrint = 0;
  unsigned long lastDeflatedFlashTime = 0; // fully-deflated first-LED flash state
  bool deflatedFlashOn = false;

  // Short smoothing hold: the mic signal oscillates fast enough that even
  // 8 rapid back-to-back samples can miss a peak on any given loop tick.
  // Once we DO see a threshold crossing, treat it as "still blowing" for a
  // brief window afterward, instead of requiring every single tick to catch it.
  const unsigned long BLOW_DETECT_HOLD_MS = 150;
  unsigned long lastThresholdCrossTime = 0;
  bool everCrossedThreshold = false;

  Serial.println("Balloon: blow into the mic continuously to inflate it!");
  balloonIntroDeflate();
  lastLoopTime = millis(); // reset - the intro was blocking, don't count its time as a tick

  while (true) {
    // 3-second exit-hold check
    updateButtonDebounce();
    bool pressedNow = isButtonPressed();
    if (pressedNow && !wasPressed) holdStart = millis();
    if (pressedNow && millis() - holdStart >= EXIT_HOLD_MS) {
      Serial.println("[balloon] exit hold triggered - returning to attract mode");
      digitalWrite(LED_BUILTIN, HIGH); // "you've held long enough, release now"
      exitIndicatorLedOn = true;
      noTone(PIN_PIEZO);
      exitAllLedsAndEnterAttract();
      return;
    }
    wasPressed = pressedNow;

    unsigned long now = millis();
    unsigned long dtMs = now - lastLoopTime;
    // Take several quick samples and use the most extreme one - a single
    // instantaneous sample can catch a "quiet" point in the waveform even
    // during a hard, loud blow, since the raw signal oscillates rapidly.
    int peakReading = 0;
    for (int s = 0; s < 8; s++) {
      int r = analogRead(PIN_MIC);
      if (r > peakReading) peakReading = r;
    }
    int dip = 500 - peakReading;

    if (dip <= MIC_BLOW_THRESHOLD) {
      lastThresholdCrossTime = now;
      everCrossedThreshold = true;
    }
    bool blowingNow = everCrossedThreshold && (now - lastThresholdCrossTime < BLOW_DETECT_HOLD_MS);

    if (blowingNow != lastBlowingState) {
      if (blowingNow) {
        Serial.print("[balloon] blow START (dip=");
        Serial.print(dip);
        Serial.println(")");
      } else {
        Serial.print("[balloon] blow PAUSED (dip=");
        Serial.print(dip);
        Serial.println(") - grace period started");
      }
      lastBlowingState = blowingNow;
    }

    if (blowingNow) {
      blowAccumulatedMs += (unsigned long)(dtMs * BALLOON_CHARGE_MULTIPLIER);
      lastBlowTime = now;
      everBlew = true;
      isDecaying = false;
    } else if (everBlew) {
      unsigned long gapMs = now - lastBlowTime;
      if (gapMs <= BALLOON_GRACE_MS) {
        // within grace period - hold steady, no decay yet
      } else {
        if (!isDecaying) {
          Serial.print("[balloon] grace expired (");
          Serial.print(gapMs);
          Serial.println("ms) - DECAYING");
          isDecaying = true;
        }
        if (blowAccumulatedMs > 0) {
          float decayAmount = BALLOON_DECAY_MS_PER_MS * dtMs;
          float newVal = (float)blowAccumulatedMs - decayAmount;
          if (newVal < 0) newVal = 0;
          blowAccumulatedMs = (unsigned long)newVal;
        }
      }
    }
    lastLoopTime = now;

    float progress = (float)blowAccumulatedMs / (float)BALLOON_TARGET_MS;
    if (progress > 1.0) progress = 1.0;

    if (now - lastStatusPrint >= 250) {
      Serial.print("[balloon] dip=");
      Serial.print(dip);
      Serial.print(" accum=");
      Serial.print(blowAccumulatedMs);
      Serial.print("ms progress=");
      Serial.print((int)(progress * 100));
      Serial.println("%");
      lastStatusPrint = now;
    }

    int litCount = (int)(progress * 5.0 + 0.5);
    if (litCount == 0) {
      // Fully deflated - flash the first LED so it's clear something is
      // still happening, rather than everything just going dark.
      if (now - lastDeflatedFlashTime >= 300) {
        deflatedFlashOn = !deflatedFlashOn;
        lastDeflatedFlashTime = now;
      }
      digitalWrite(PIN_BANK[0], deflatedFlashOn ? HIGH : LOW);
      for (int i = 1; i < 5; i++) digitalWrite(PIN_BANK[i], LOW);
    } else {
      for (int i = 0; i < 5; i++) digitalWrite(PIN_BANK[i], (i < litCount) ? HIGH : LOW);
    }

    if (now - lastToneUpdate >= 50) {
      if (blowAccumulatedMs > 0) {
        int freq = 150 + (int)(progress * 750); // 150Hz -> 900Hz pitch bend
        tone(PIN_PIEZO, freq);
      } else {
        noTone(PIN_PIEZO);
      }
      lastToneUpdate = now;
    }

    if (progress >= 1.0) {
      popBalloon();
      Serial.println("Win! Back to attract mode.");
      enterAttractMode();
      return;
    }

    delay(15);
  }
}

void popBalloon() {
  Serial.println("*** POP! Balloon complete! ***");

  // Shrill "old phone ringing" effect: rapid alternation between two notes
  // a major third apart, with a light show, for 2 seconds.
  const unsigned long RING_MS = 2000;
  const int NOTE_LOW = 880;                                    // A5
  const int NOTE_HIGH = (int)(880 * pow(2.0, 4.0 / 12.0));      // major third above (~C#6)
  const unsigned long RING_NOTE_MS = 90;                        // fast alternation

  unsigned long start = millis();
  bool toggle = false;
  while (millis() - start < RING_MS) {
    int freq = toggle ? NOTE_HIGH : NOTE_LOW;
    tone(PIN_PIEZO, freq, RING_NOTE_MS);
    clearAllLeds();
    int numToLight = random(2, NUM_ALL_LEDS + 1);
    for (int k = 0; k < numToLight; k++) digitalWrite(ALL_LEDS[random(0, NUM_ALL_LEDS)], HIGH);

    toggle = !toggle;
    delay(RING_NOTE_MS);
  }

  noTone(PIN_PIEZO);
  clearAllLeds();
}

// ---------------- Slot 4: RoboTheremin ----------------

// Adapted from Gerard's existing sketch for this same badge. Tilt on the
// X-axis controls pitch, Y-axis controls volume/duty and lights the
// forward/back indicator LEDs. Runs until the 3s exit hold. Uses the raw
// lis.x/lis.y accelerometer API (via lis.read()) rather than getEvent(),
// same as the rest of the Gerard's code - both APIs work fine on the same
// lis object.
void roboThereminGame() {
  Serial.println("RoboTheremin: tilt for pitch/volume. Hold button 3s to exit.");

  unsigned long holdStart = 0;
  bool wasPressed = false;

  while (true) {
    // 3-second exit-hold check
    updateButtonDebounce();
    bool pressedNow = isButtonPressed();
    if (pressedNow && !wasPressed) holdStart = millis();
    if (pressedNow && millis() - holdStart >= EXIT_HOLD_MS) {
      digitalWrite(LED_BUILTIN, HIGH); // "you've held long enough, release now"
      exitIndicatorLedOn = true;
      noTone(PIN_PIEZO);
      exitAllLedsAndEnterAttract();
      return;
    }
    wasPressed = pressedNow;

    lis.read();
    int xVal = (int)lis.x;
    int yVal = (int)lis.y;

    // 1. Audio mapping
    int pitch = map(constrain(xVal, -10000, 10000), -10000, 10000, 100, 2000);
    int volumeDuty = map(constrain(yVal, -10000, 10000), -10000, 10000, 10, 90);

    // 2. Pitch visuals (X-axis) across the 5 mouth LEDs
    int ledPos = (int)(((float)(constrain(xVal, -10000, 10000) + 10000) / 20000.0) * 4.0 + 0.5);
    ledPos = constrain(ledPos, 0, 4);
    for (int i = 0; i < 5; i++) {
      digitalWrite(PIN_BANK[i], (i == ledPos) ? HIGH : LOW);
    }

    // 3. Volume/direction visuals (Y-axis)
    clearFaceLeds();
    if (yVal > 2000) {
      digitalWrite(PIN_LED_FOREHEAD, HIGH);
    } else if (yVal < -2000) {
      digitalWrite(PIN_LED_LEFTEYE, HIGH);
      digitalWrite(PIN_LED_RIGHTEYE, HIGH);
    }

    // 4. Audio output
    tone(PIN_PIEZO, pitch);
    delay(100 - volumeDuty);
    noTone(PIN_PIEZO);
    delay(volumeDuty / 4);
  }
}

// ---------------- Percussion synthesis (DAC-based) ----------------

// The piezo sits on A0, which is also the SAMD21's real hardware DAC pin -
// not just a PWM-capable GPIO. tone() can only make a flat square wave at
// one frequency; analogWrite() here drives the actual DAC, so we can shape
// real amplitude envelopes and pitch sweeps for much more percussive,
// drum-machine-like hits instead of plain beeps.

// The SAMD21's DAC output on A0 uses a separate analog driver path from the
// normal digital pin mux that pinMode() controls - pinMode() alone does NOT
// disconnect/disable the DAC hardware, so it can keep quietly loading the
// pin afterward and dampen tone()'s square wave. Explicitly disable the DAC
// peripheral itself so the pin is genuinely free for digital use again.
void disablePiezoDAC() {
  DAC->CTRLA.bit.ENABLE = 0;
  while (DAC->STATUS.bit.SYNCBUSY);
  pinMode(PIN_PIEZO, OUTPUT);
}

// ---------------- Mixed percussion voices (for the Drum Machine) ----------------

// Kick/snare/hi-hat, restructured as triggerable "voices" that a continuous
// mixer renders together in real time - this is what lets multiple banks
// sharing a step sound genuinely simultaneous instead of staggered one
// after another.

struct DrumVoice {
  bool active;
  unsigned long startTimeUs;
  float phase; // only used by the kick's continuous tone
};

DrumVoice mixKickVoice = {false, 0, 0};
DrumVoice mixSnareVoice = {false, 0, 0};
DrumVoice mixHihatVoice = {false, 0, 0};
unsigned long mixLastTickUs = 0;

void triggerMixKick()  { mixKickVoice.active = true;  mixKickVoice.startTimeUs = micros();  mixKickVoice.phase = 0.0; }
void triggerMixSnare() { mixSnareVoice.active = true; mixSnareVoice.startTimeUs = micros(); }
void triggerMixHihat() { mixHihatVoice.active = true; mixHihatVoice.startTimeUs = micros(); }

// Call every loop iteration while the drum machine is running. Mixes every
// currently-active voice together and writes ONE combined sample to the DAC.
void updateDrumMix() {
  unsigned long nowUs = micros();
  unsigned long dtUs = nowUs - mixLastTickUs;
  mixLastTickUs = nowUs;
  float dtSec = dtUs / 1000000.0;

  int mixed = 0;
  bool anyActive = false;

  // Kick: continuous phase-accumulated square wave, pitch sweep + decay
  if (mixKickVoice.active) {
    const unsigned long DURATION_US = 120000;
    unsigned long elapsedUs = nowUs - mixKickVoice.startTimeUs;
    if (elapsedUs >= DURATION_US) {
      mixKickVoice.active = false;
    } else {
      float t = (float)elapsedUs / DURATION_US;
      const float START_FREQ = 180.0;
      const float END_FREQ = 45.0;
      float freq = START_FREQ * pow(END_FREQ / START_FREQ, t);
      float amp = pow(1.0 - t, 2.0);
      mixKickVoice.phase += freq * dtSec;
      mixKickVoice.phase -= (long)mixKickVoice.phase; // wrap into 0..1
      int sign = (mixKickVoice.phase < 0.5) ? 1 : -1;
      mixed += (int)(sign * amp * 120);
      anyActive = true;
    }
  }

  // Snare: noise burst with decay
  if (mixSnareVoice.active) {
    const unsigned long DURATION_US = 90000;
    unsigned long elapsedUs = nowUs - mixSnareVoice.startTimeUs;
    if (elapsedUs >= DURATION_US) {
      mixSnareVoice.active = false;
    } else {
      float t = (float)elapsedUs / DURATION_US;
      float amp = pow(1.0 - t, 1.5);
      mixed += (int)(random(-120, 121) * amp);
      anyActive = true;
    }
  }

  // Hi-hat: shorter, brighter noise burst with quicker decay
  if (mixHihatVoice.active) {
    const unsigned long DURATION_US = 50000;
    unsigned long elapsedUs = nowUs - mixHihatVoice.startTimeUs;
    if (elapsedUs >= DURATION_US) {
      mixHihatVoice.active = false;
    } else {
      float t = (float)elapsedUs / DURATION_US;
      float amp = pow(1.0 - t, 2.0);
      mixed += (int)(random(-100, 101) * amp);
      anyActive = true;
    }
  }

  if (anyActive) {
    if (mixed > 127) mixed = 127;
    if (mixed < -127) mixed = -127;
    analogWrite(PIN_PIEZO, 128 + mixed);
  } else {
    analogWrite(PIN_PIEZO, 128);
  }
}

void stopDrumMix() {
  mixKickVoice.active = false;
  mixSnareVoice.active = false;
  mixHihatVoice.active = false;
  analogWrite(PIN_PIEZO, 128);
  disablePiezoDAC();
}

// ---------------- Slot 5: Drum Machine / Looper ----------------

// 3 banks x 4 steps (4/4, not 5/4), all locked to one shared step-clock
// (quantized together). Touch input is the 4 INNER teeth (tooth2, tooth3,
// tooth4/ADC_BATTERY, tooth5), in physical left-to-right order. Since
// tooth4 has no LED of its own, the step LEDs deliberately do NOT try to
// match physical touch position - instead they use 4 of the 5 available
// teeth LEDs (light1, light2, light4, light5), skipping the middle one
// (light3) entirely, so every step still gets a real LED. Touch and LED
// lookups use SEPARATE index arrays since they're no longer 1:1 by position.
const int DRUM_STEP_TOUCH_INDEX[4] = {1, 2, 3, 4}; // touchPads[] indices: pad2, pad3, pad4/tooth4, pad5
const int DRUM_STEP_LED_INDEX[4] = {0, 1, 3, 4};   // PIN_BANK[] indices: light1, light2, light4, light5
const int NUM_DRUM_STEPS = 4;

void updateBankIndicatorLed(int bank) {
  digitalWrite(PIN_LED_LEFTEYE, bank == 0 ? HIGH : LOW);    // left eye = bank 1
  digitalWrite(PIN_LED_FOREHEAD, bank == 1 ? HIGH : LOW); // forehead = bank 2
  digitalWrite(PIN_LED_RIGHTEYE, bank == 2 ? HIGH : LOW); // right eye = bank 3
}

void refreshTeethLedsForBank(bool pattern[4]) {
  allBankOff();
  for (int s = 0; s < NUM_DRUM_STEPS; s++) {
    int ledIdx = DRUM_STEP_LED_INDEX[s];
    digitalWrite(PIN_BANK[ledIdx], pattern[s] ? HIGH : LOW);
  }
}

void drumMachineGame() {
  const unsigned long STEP_INTERVAL_MS = 400; // tunable tempo

  bool bankPattern[3][NUM_DRUM_STEPS] = {
    {false, false, false, false},
    {false, false, false, false},
    {false, false, false, false}
  };
  int currentBank = 0;
  int currentStep = 0;
  int flashingLedIdx = -1; // no LED currently waiting to come back (this is a non-blocking solution to avoid delay())
  unsigned long lastStepTime = millis();
  unsigned long flashEndTime = 0;

  bool padWasActive[NUM_DRUM_STEPS] = {false, false, false, false};       // debounced/stable state
  bool padRawLastReading[NUM_DRUM_STEPS] = {false, false, false, false};  // last raw (undebounced) reading
  unsigned long padLastChangeTime[NUM_DRUM_STEPS] = {0, 0, 0, 0};        // when the raw reading last changed
  const unsigned long PAD_DEBOUNCE_MS = 40; // require a stable reading this long before accepting it
  unsigned long pressStartTime = 0;
  bool wasPressedLocal = false;

  updateBankIndicatorLed(currentBank);
  refreshTeethLedsForBank(bankPattern[currentBank]);
  Serial.println("Drum Machine: touch the 4 inner pads to toggle steps, press button for next bank, hold 3s to exit.");
  mixLastTickUs = micros(); // avoid a huge bogus dt on the mixer's first tick

  while (true) {
    // Button: short press = next bank, 3s hold = exit to attract
    updateButtonDebounce();
    bool pressedNow = isButtonPressed();
    if (pressedNow && !wasPressedLocal) pressStartTime = millis();
    if (pressedNow && millis() - pressStartTime >= EXIT_HOLD_MS) {
      digitalWrite(LED_BUILTIN, HIGH); // "you've held long enough, release now"
      exitIndicatorLedOn = true;
      stopDrumMix();
      exitAllLedsAndEnterAttract();
      return;
    }
    if (!pressedNow && wasPressedLocal) {
      unsigned long heldMs = millis() - pressStartTime;
      if (heldMs < EXIT_HOLD_MS) {
        currentBank = (currentBank + 1) % 3;
        updateBankIndicatorLed(currentBank);
        refreshTeethLedsForBank(bankPattern[currentBank]);
        tone(PIN_PIEZO, 1500, 40); // bank-switch confirmation blip
        Serial.print("[drum] Bank ");
        Serial.print(currentBank + 1);
        Serial.println(" selected");
      }
    }
    wasPressedLocal = pressedNow;

    // Touch pads toggle steps for the CURRENTLY SELECTED bank (inner 4 pads)
    for (int s = 0; s < NUM_DRUM_STEPS; s++) {
      int touchIdx = DRUM_STEP_TOUCH_INDEX[s];
      bool rawActive = touchIsActive(touchIdx);

      if (rawActive != padRawLastReading[s]) {
        padLastChangeTime[s] = millis();
        padRawLastReading[s] = rawActive;
      }

      bool stableActive = padWasActive[s]; // default: hold the current stable state
      if (millis() - padLastChangeTime[s] > PAD_DEBOUNCE_MS) {
        stableActive = rawActive; // reading has held steady long enough - accept it
      }

      if (stableActive && !padWasActive[s]) {
        bankPattern[currentBank][s] = !bankPattern[currentBank][s];
        int ledIdx = DRUM_STEP_LED_INDEX[s];
        digitalWrite(PIN_BANK[ledIdx], bankPattern[currentBank][s] ? HIGH : LOW);
        Serial.print("[drum] Bank ");
        Serial.print(currentBank + 1);
        Serial.print(" step ");
        Serial.print(s + 1);
        Serial.println(bankPattern[currentBank][s] ? " ON" : " OFF");
      }
      padWasActive[s] = stableActive;
    }

    // Shared, quantized step-clock - all 3 banks locked to the same grid
    unsigned long now = millis();
    int stepLedIdx = DRUM_STEP_LED_INDEX[currentStep];
    if (now - lastStepTime >= STEP_INTERVAL_MS) {
      lastStepTime += STEP_INTERVAL_MS; // stay grid-aligned instead of drifting

      bool playingOnCurrentBank = bankPattern[currentBank][currentStep];
      //int stepLedIdx = DRUM_STEP_LED_INDEX[currentStep];

      if (playingOnCurrentBank && stepLedIdx != -1) {
        digitalWrite(PIN_BANK[stepLedIdx], LOW); // short flash off, decoupled from actual hit duration
        //delay(40);
        flashingLedIdx = stepLedIdx;
        flashEndTime = millis() + 40; // turn it back on in 40ms
        //digitalWrite(PIN_BANK[stepLedIdx], HIGH);
      }

      for (int b = 0; b < 3; b++) {
        if (bankPattern[b][currentStep]) {
          if (b == 0) triggerMixKick();
          else if (b == 1) triggerMixSnare();
          else triggerMixHihat();
        }
      }

      currentStep = (currentStep + 1) % NUM_DRUM_STEPS;
    }
    if (flashingLedIdx != -1 && millis() >= flashEndTime) {
      refreshTeethLedsForBank(bankPattern[currentBank]);
      flashingLedIdx = -1;
    }

    updateDrumMix(); // continuously render whatever voices are currently active, mixed together
  }
}
// ---------------- Slot 6: Ocarina ----------------
uint8_t digitalOcarinaGetState(){
  static uint8_t state = 0;
  static const int PAD_COUNT = 6;
  static bool stateArray[PAD_COUNT] = {0};
  static bool padWasActive[PAD_COUNT] = {0};       // debounced/stable state
  static bool padRawLastReading[PAD_COUNT] = {0};  // last raw (undebounced) reading
  static unsigned long padLastChangeTime[PAD_COUNT] = {0};        // when the raw reading last changed
  static const unsigned long PAD_DEBOUNCE_MS = 10; // require a stable reading this long before accepting it

 for (int i = 0; i < PAD_COUNT; i++) {
    bool rawActive = touchIsActive(i);

    if (rawActive != padRawLastReading[i]) {
        padLastChangeTime[i] = millis();
        padRawLastReading[i] = rawActive;
    }

    if (millis() - padLastChangeTime[i] > PAD_DEBOUNCE_MS) {
        padWasActive[i] = rawActive;
    }
}

state =
    (padWasActive[0]  | padWasActive[1])       |
    ((padWasActive[2] | padWasActive[3]) << 1) |
    ((padWasActive[4] | padWasActive[5]) << 2);
    
  return state;
}

void digitalOcarina() {
    Serial.println("Digital Ocarina: Press combos of touch pads and blow into mic to play notes. Hold button 3s to exit.");
  unsigned long holdStart = 0;
  bool wasPressed = false;
  bool isPlaying = false;
  unsigned long previousMillis = 0;
  uint16_t currNote = 0;
  uint8_t touchPadState = 0;
unsigned long lastBlowTime = 0;
const unsigned long HOLD_TIME = 300; // ms
  allBankOff();
  while (true) {
    // 3-second exit-hold check
    updateButtonDebounce();
    bool pressedNow = isButtonPressed();
    if (pressedNow && !wasPressed) holdStart = millis();
    if (pressedNow && millis() - holdStart >= EXIT_HOLD_MS) {
      digitalWrite(LED_BUILTIN, HIGH); // "you've held long enough, release now"
      exitIndicatorLedOn = true;
      noTone(PIN_PIEZO);
      exitAllLedsAndEnterAttract();
      return;
    }
    wasPressed = pressedNow;
    
  unsigned long currentMillis = millis();


  if (checkMicTriggered()) {
    lastBlowTime = millis();
  }

  if ((millis() - lastBlowTime < HOLD_TIME) && !isPlaying) {
    tone(PIN_PIEZO, currNote);
    isPlaying = true;
    Serial.println(currNote);
  }

  if ((millis() - lastBlowTime >= HOLD_TIME) && isPlaying) {
    noTone(PIN_PIEZO);
    isPlaying = false;
  }

  touchPadState = digitalOcarinaGetState();
  switch(touchPadState){
  case 0b001: currNote = NOTE_C4; break;
  case 0b010: currNote = NOTE_D4; break;
  case 0b011: currNote = NOTE_E4; break;
  case 0b100: currNote = NOTE_F4; break;
  case 0b101: currNote = NOTE_G4; break;
  case 0b110: currNote = NOTE_A4; break;
  case 0b111: currNote = NOTE_B4; break;
  }
  }
}
// ---------------- Touch pads ----------------

// FreeTouch can be pretty noisy. Humidity varies. Pull a few samples to calibrate.
void calibrateTouch() {
  for (int i = 0; i < NUM_UNIQUE_PADS; i++) {
    long sum = 0;
    for (int j = 0; j < 5; j++) {
      sum += touchPads[i].measure();
      delay(10);
    }
    touchBaseline[i] = sum / 5;
  }
}

// Swipe detection uses the 4 INNER touch channels, in true physical
// left-to-right order: A2, A3, ADC_BATTERY, A4. The 2 true outer
// channels (A1, A5) are still excluded since they're small/easy to miss at
// the edges of the mouth. Window stays at SWIPE_WINDOW_MS even
// though there are now 3 transitions to cover instead of 2 - tighter, but
// intentionally left as-is for testing rather than pre-loosened.
const int SWIPE_PAD_INDICES[4] = {1, 2, 3, 4}; // touchPads[] indices: pad2, pad3, pad4/tooth4, pad5
const int NUM_SWIPE_PADS = 4;

// Returns 0 = no swipe yet, 1 = full left-to-right swipe, 2 = full right-to-left swipe.
// Requires all 4 inner pads to activate, in spatial order, within SWIPE_WINDOW_MS.
// A single touch (or touches out of order/too spread out) will NOT trigger this.
int detectSlide() {
  for (int i = 0; i < NUM_UNIQUE_PADS; i++) {
    bool isActive = touchIsActive(i);
    if (isActive && !touchWasActive[i]) {
      touchActivationTime[i] = millis(); // rising edge only
    }
    touchWasActive[i] = isActive;
  }

  for (int s = 0; s < NUM_SWIPE_PADS; s++) {
    if (touchActivationTime[SWIPE_PAD_INDICES[s]] == 0) return 0; // not all 4 touched yet
  }

  unsigned long minT = touchActivationTime[SWIPE_PAD_INDICES[0]];
  unsigned long maxT = touchActivationTime[SWIPE_PAD_INDICES[0]];
  for (int s = 1; s < NUM_SWIPE_PADS; s++) {
    unsigned long t = touchActivationTime[SWIPE_PAD_INDICES[s]];
    if (t < minT) minT = t;
    if (t > maxT) maxT = t;
  }
  if (maxT - minT > SWIPE_WINDOW_MS) {
    for (int s = 0; s < NUM_SWIPE_PADS; s++) touchActivationTime[SWIPE_PAD_INDICES[s]] = 0;
    return 0;
  }

  bool leftToRight = true, rightToLeft = true;
  for (int s = 0; s < NUM_SWIPE_PADS - 1; s++) {
    unsigned long tCur = touchActivationTime[SWIPE_PAD_INDICES[s]];
    unsigned long tNext = touchActivationTime[SWIPE_PAD_INDICES[s + 1]];
    if (tCur > tNext) leftToRight = false;
    if (tCur < tNext) rightToLeft = false;
  }

  for (int s = 0; s < NUM_SWIPE_PADS; s++) touchActivationTime[SWIPE_PAD_INDICES[s]] = 0; // reset

  if (leftToRight) return 1;
  if (rightToLeft) return 2;
  return 0; // touched all 4 but not in a clean order
}

// ---------------- LED bank ----------------

void allBankOff() {
  for (int i = 0; i < 5; i++) digitalWrite(PIN_BANK[i], LOW);
}

// Clears the 3 face LEDs (forehead + both eyes) and SAO's - the other half of "clear
// everything" alongside allBankOff(), which only handles the teeth.
void clearFaceLeds() {
  digitalWrite(PIN_LED_FOREHEAD, LOW);
  digitalWrite(PIN_LED_LEFTEYE, LOW);
  digitalWrite(PIN_LED_RIGHTEYE, LOW);
  digitalWrite(PIN_LED_SAO, LOW);
}

// The very common "clear every LED on the badge" combo.
void clearAllLeds() {
  allBankOff();
  clearFaceLeds();
}

// The shared "give up, go back to Attract mode" cleanup used by every
// subroutine's 3-second exit hold (and the global kill switch). Clears all
// 8 LEDs and hands off to enterAttractMode(). Callers still handle their
// own audio cleanup first (noTone() vs stopDrumMix()) since that varies.
void exitAllLedsAndEnterAttract() {
  clearAllLeds();
  enterAttractMode();
}

// Forces every LED pin back to plain GPIO output before clearing it -
// pinMode(OUTPUT) is what actually releases a pin that analogWrite() may
// have left attached to a PWM timer; plain digitalWrite() alone isn't
// reliably enough (confirmed the hard way more than once tonight). Used
// anywhere Attract mode's PWM-based light shows (modes 5/6) might hand off
// to something that only ever uses digitalWrite().
void resetAllLedsToDigitalOutput() {
  for (int i = 0; i < NUM_ALL_LEDS; i++) {
    pinMode(ALL_LEDS[i], OUTPUT);
    digitalWrite(ALL_LEDS[i], LOW);
  }
}

void animateBank(bool leftToRight) {
  allBankOff();
  if (leftToRight) {
    for (int i = 0; i < 5; i++) {
      digitalWrite(PIN_BANK[i], HIGH);
      delay(60);
      digitalWrite(PIN_BANK[i], LOW);
    }
  } else {
    for (int i = 4; i >= 0; i--) {
      digitalWrite(PIN_BANK[i], HIGH);
      delay(60);
      digitalWrite(PIN_BANK[i], LOW);
    }
  }
}
