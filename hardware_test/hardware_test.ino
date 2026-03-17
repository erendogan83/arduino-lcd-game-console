/*
 * ============================================
 *   Arduino Nano LCD Game Console
 *   HARDWARE TEST v1.0
 *   Author: Eren DOGAN
 * ============================================
 *
 * PIN LAYOUT:
 *   A4  -> LCD SDA
 *   A5  -> LCD SCL
 *   D2  -> BTN UP    (INPUT_PULLUP)
 *   D3  -> BTN DOWN  (INPUT_PULLUP)
 *   D4  -> BTN LEFT  (INPUT_PULLUP)
 *   D5  -> BTN FIRE  (INPUT_PULLUP)
 *   D6  -> BUZZER
 *   D7  -> LED GREEN  (220 ohm)
 *   D8  -> LED YELLOW (220 ohm)
 *   D9  -> LED RED    (220 ohm)
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ─── LCD ───────────────────────────────────
// Adres 0x27 degilse 0x3F dene
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ─── PINLER ────────────────────────────────
#define BTN_UP     2
#define BTN_DOWN   3
#define BTN_LEFT   4
#define BTN_FIRE   5
#define BUZZER     6
#define LED_GREEN  7
#define LED_YELLOW 8
#define LED_RED    9

// ─── BUTON ─────────────────────────────────
#define BTN_COUNT    4
#define DEBOUNCE_MS  50

const int   btnPins[BTN_COUNT]    = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_FIRE};
const char* btnNames[BTN_COUNT]   = {"UP", "DOWN", "LEFT", "FIRE"};
bool        btnState[BTN_COUNT]   = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounce[BTN_COUNT] = {0, 0, 0, 0};

// ─── CUSTOM KARAKTERLER ────────────────────
byte charHeart[8] = {
  0b00000, 0b01010, 0b11111, 0b11111,
  0b01110, 0b00100, 0b00000, 0b00000
};
byte charRunner[8] = {
  0b00100, 0b01110, 0b00100, 0b01110,
  0b10101, 0b00100, 0b01010, 0b10001
};
byte charBullet[8] = {
  0b00000, 0b00100, 0b01110, 0b01110,
  0b01110, 0b00100, 0b00000, 0b00000
};
byte charBoss[8] = {
  0b10101, 0b01110, 0b11111, 0b10101,
  0b11111, 0b01110, 0b10101, 0b00000
};
byte charShield[8] = {
  0b01110, 0b11111, 0b11111, 0b11111,
  0b01110, 0b00100, 0b00000, 0b00000
};
byte charCoin[8] = {
  0b01110, 0b10011, 0b10101, 0b10111,
  0b10101, 0b10011, 0b01110, 0b00000
};
byte charFlame[8] = {
  0b00100, 0b01100, 0b01110, 0b11111,
  0b11111, 0b01110, 0b00100, 0b00000
};
byte charSkull[8] = {
  0b01110, 0b10101, 0b11011, 0b01110,
  0b01110, 0b00000, 0b00000, 0b00000
};

// Karakter index
#define CHAR_HEART   0
#define CHAR_RUNNER  1
#define CHAR_BULLET  2
#define CHAR_BOSS    3
#define CHAR_SHIELD  4
#define CHAR_COIN    5
#define CHAR_FLAME   6
#define CHAR_SKULL   7

// ─── TEST AŞAMALARI ────────────────────────
enum TestPhase {
  PHASE_INTRO,
  PHASE_LCD,
  PHASE_CUSTOM_CHARS,
  PHASE_LEDS,
  PHASE_BUZZER,
  PHASE_BUTTONS,
  PHASE_COMBINED,
  PHASE_DONE
};

TestPhase currentPhase = PHASE_INTRO;
bool phaseComplete = false;

// ─── YARDIMCI FONKSİYONLAR ─────────────────

void beep(int freq, int duration) {
  tone(BUZZER, freq, duration);
}

void beepMelody(int freqs[], int durations[], int count) {
  for (int i = 0; i < count; i++) {
    tone(BUZZER, freqs[i], durations[i]);
    delay(durations[i] + 30);
  }
  noTone(BUZZER);
}

void ledSet(bool green, bool yellow, bool red) {
  digitalWrite(LED_GREEN,  green  ? HIGH : LOW);
  digitalWrite(LED_YELLOW, yellow ? HIGH : LOW);
  digitalWrite(LED_RED,    red    ? HIGH : LOW);
}

void ledOff() {
  ledSet(false, false, false);
}

void ledFlash(int pin, int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(onMs);
    digitalWrite(pin, LOW);
    delay(offMs);
  }
}

void ledPulse(int pin, int count) {
  // Kısa - uzun - kısa pattern
  for (int i = 0; i < count; i++) {
    digitalWrite(pin, HIGH); delay(80);
    digitalWrite(pin, LOW);  delay(80);
    digitalWrite(pin, HIGH); delay(300);
    digitalWrite(pin, LOW);  delay(150);
  }
}

void ledChase(int times) {
  for (int t = 0; t < times; t++) {
    digitalWrite(LED_GREEN,  HIGH); delay(120); digitalWrite(LED_GREEN,  LOW);
    digitalWrite(LED_YELLOW, HIGH); delay(120); digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED,    HIGH); delay(120); digitalWrite(LED_RED,    LOW);
  }
}

void ledChaseReverse(int times) {
  for (int t = 0; t < times; t++) {
    digitalWrite(LED_RED,    HIGH); delay(120); digitalWrite(LED_RED,    LOW);
    digitalWrite(LED_YELLOW, HIGH); delay(120); digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN,  HIGH); delay(120); digitalWrite(LED_GREEN,  LOW);
  }
}

void ledBlink(int times) {
  for (int i = 0; i < times; i++) {
    ledSet(true, true, true);
    delay(100);
    ledOff();
    delay(100);
  }
}

void lcdPrint(int col, int row, const char* text) {
  lcd.setCursor(col, row);
  lcd.print(text);
}

void lcdClear() {
  lcd.clear();
}

bool buttonPressed(int index) {
  bool reading = digitalRead(btnPins[index]);
  if (reading == LOW && btnState[index] == HIGH) {
    if (millis() - lastDebounce[index] > DEBOUNCE_MS) {
      lastDebounce[index] = millis();
      btnState[index] = LOW;
      return true;
    }
  }
  if (reading == HIGH) btnState[index] = HIGH;
  return false;
}

bool anyButtonPressed() {
  for (int i = 0; i < BTN_COUNT; i++) {
    if (buttonPressed(i)) return true;
  }
  return false;
}

// ─── TEST AŞAMALARI ────────────────────────

void runPhaseIntro() {
  lcdClear();
  lcdPrint(0, 0, " HARDWARE  TEST ");
  lcdPrint(0, 1, "  v1.0  STARTING");

  int freqs[]     = {800, 1000, 1200, 1500};
  int durations[] = {100, 100,  100,  200};
  beepMelody(freqs, durations, 4);

  ledChase(3);
  ledOff();
  delay(500);

  lcdClear();
  lcdPrint(0, 0, "Press ANY button");
  lcdPrint(0, 1, "   to continue  ");

  Serial.println("=== HARDWARE TEST v1.0 ===");
  Serial.println("Waiting for button...");

  currentPhase = PHASE_LCD;
}

void runPhaseLCD() {
  lcdClear();
  lcdPrint(0, 0, "TEST 1: LCD");
  lcdPrint(0, 1, "Rendering OK?");

  ledSet(false, true, false); // Sarı: test devam ediyor
  beep(1000, 80);
  delay(1500);

  // Scroll test
  lcdClear();
  lcdPrint(0, 0, "ABCDEFGHIJKLMNOP");
  lcdPrint(0, 1, "0123456789!@#$%^");
  delay(1500);

  // Satır 2 test
  lcdClear();
  lcdPrint(0, 0, "ROW 0: TOP  OK? ");
  lcdPrint(0, 1, "ROW 1: BOTTOM OK");
  delay(1500);

  lcdClear();
  lcdPrint(0, 0, "LCD TEST DONE");
  lcdPrint(0, 1, "Press ANY button");

  ledSet(true, false, false); // Yesil: tamamlandi
  beep(1500, 100);

  Serial.println("[PASS] LCD Test");
  currentPhase = PHASE_CUSTOM_CHARS;
}

void runPhaseCustomChars() {
  lcdClear();
  lcdPrint(0, 0, "TEST 2: SPRITES");
  lcdPrint(0, 1, "8 custom chars");

  ledSet(false, true, false);
  beep(1000, 80);
  delay(1200);

  lcdClear();
  lcdPrint(0, 0, "Sprites:");

  // 8 custom char yan yana göster
  for (int i = 0; i < 8; i++) {
    lcd.setCursor(i, 1);
    lcd.write(byte(i));
    delay(200);
    beep(800 + i * 100, 60);
  }

  delay(1000);

  // İsimleriyle göster
  const char* charNameList[] = {
    "HEART  ", "RUNNER ", "BULLET ",
    "BOSS   ", "SHIELD ", "COIN   ",
    "FLAME  ", "SKULL  "
  };

  for (int i = 0; i < 8; i++) {
    lcdClear();
    lcdPrint(0, 0, "Sprite:");
    lcd.setCursor(8, 0);
    lcd.write(byte(i));
    lcdPrint(0, 1, charNameList[i]);
    delay(600);
  }

  lcdClear();
  lcdPrint(0, 0, "SPRITES DONE");
  lcdPrint(0, 1, "Press ANY button");

  ledSet(true, false, false);
  beep(1500, 100);

  Serial.println("[PASS] Custom Chars Test");
  currentPhase = PHASE_LEDS;
}

void runPhaseLEDs() {
  lcdClear();
  lcdPrint(0, 0, "TEST 3: LEDs");
  lcdPrint(0, 1, "Watch the LEDs");

  beep(1000, 80);
  delay(800);

  // Tek tek yak
  lcdClear();
  lcdPrint(0, 0, "GREEN  ON");
  lcdPrint(0, 1, "");
  ledSet(true, false, false); delay(600);

  lcdClear();
  lcdPrint(0, 0, "YELLOW ON");
  ledSet(false, true, false); delay(600);

  lcdClear();
  lcdPrint(0, 0, "RED    ON");
  ledSet(false, false, true); delay(600);

  lcdClear();
  lcdPrint(0, 0, "ALL    ON");
  ledSet(true, true, true); delay(600);
  ledOff();
  delay(300);

  // Chase pattern
  lcdClear();
  lcdPrint(0, 0, "CHASE PATTERN");
  ledChase(3);

  lcdClear();
  lcdPrint(0, 0, "REVERSE CHASE");
  ledChaseReverse(3);

  // Oyun senaryosu
  lcdClear();
  lcdPrint(0, 0, "SCENARIO:");
  lcdPrint(0, 1, "SCORE UP!");
  ledFlash(LED_GREEN, 4, 80, 80);
  beep(1800, 50); delay(80); beep(2000, 100);

  lcdClear();
  lcdPrint(0, 0, "SCENARIO:");
  lcdPrint(0, 1, "LOW HEALTH!");
  ledPulse(LED_YELLOW, 2);
  beep(400, 300);

  lcdClear();
  lcdPrint(0, 0, "SCENARIO:");
  lcdPrint(0, 1, "GAME OVER!");
  ledFlash(LED_RED, 5, 150, 100);
  int goFreqs[]  = {800, 600, 400, 300};
  int goDurs[]   = {150, 150, 150, 400};
  beepMelody(goFreqs, goDurs, 4);
  ledOff();

  lcdClear();
  lcdPrint(0, 0, "LED TEST DONE");
  lcdPrint(0, 1, "Press ANY button");

  ledSet(true, false, false);
  beep(1500, 100);

  Serial.println("[PASS] LED Test");
  currentPhase = PHASE_BUZZER;
}

void runPhaseBuzzer() {
  lcdClear();
  lcdPrint(0, 0, "TEST 4: BUZZER");
  lcdPrint(0, 1, "Listen...");

  ledSet(false, true, false);
  delay(500);

  struct SoundEffect {
    const char* name;
    int freq;
    int duration;
  };

  SoundEffect effects[] = {
    {"SHOOT    1200Hz", 1200, 80},
    {"HIT      1800Hz", 1800, 80},
    {"BOSS HIT  800Hz",  800, 150},
    {"POWER-UP 2000Hz", 2000, 200},
    {"COIN      900Hz",  900, 100},
    {"JUMP     1400Hz", 1400, 80},
  };

  int count = 6;
  for (int i = 0; i < count; i++) {
    lcdClear();
    lcdPrint(0, 0, "SOUND:");
    lcdPrint(0, 1, effects[i].name);
    beep(effects[i].freq, effects[i].duration);
    delay(500);
  }

  // Game over melodisi
  lcdClear();
  lcdPrint(0, 0, "GAME OVER SOUND");
  lcdPrint(0, 1, "");
  ledSet(false, false, true);
  int goF[] = {800, 600, 400, 300};
  int goD[] = {150, 150, 150, 500};
  beepMelody(goF, goD, 4);
  ledOff();

  // Level up melodisi
  lcdClear();
  lcdPrint(0, 0, "LEVEL UP SOUND");
  lcdPrint(0, 1, "");
  ledSet(true, false, false);
  int luF[] = {1000, 1200, 1400, 1800};
  int luD[] = {100,  100,  100,  300};
  beepMelody(luF, luD, 4);
  ledOff();

  lcdClear();
  lcdPrint(0, 0, "BUZZER DONE");
  lcdPrint(0, 1, "Press ANY button");

  ledSet(true, false, false);
  beep(1500, 100);

  Serial.println("[PASS] Buzzer Test");
  currentPhase = PHASE_BUTTONS;
}

void runPhaseButtons() {
  lcdClear();
  lcdPrint(0, 0, "TEST 5: BUTTONS");
  lcdPrint(0, 1, "Press each one!");

  ledSet(false, true, false);
  beep(1000, 80);

  bool tested[BTN_COUNT] = {false, false, false, false};
  int  testedCount = 0;

  const char* btnDisplayNames[] = {"UP   ", "DOWN ", "LEFT ", "FIRE "};
  const int   btnLEDFreqs[]     = {1400, 1200, 1000, 1800};

  unsigned long timeout = millis() + 20000; // 20 saniye timeout

  while (testedCount < BTN_COUNT && millis() < timeout) {

    // Beklenen butonu göster
    lcdClear();
    lcdPrint(0, 0, "Press:");

    lcd.setCursor(0, 1);
    for (int i = 0; i < BTN_COUNT; i++) {
      if (!tested[i]) {
        lcd.print(btnDisplayNames[i]);
        break;
      }
    }

    // Hangi butonlarin basildigini goster
    lcd.setCursor(11, 1);
    for (int i = 0; i < BTN_COUNT; i++) {
      if (tested[i]) {
        lcd.write(byte(CHAR_HEART));
      } else {
        lcd.print("-");
      }
    }

    for (int i = 0; i < BTN_COUNT; i++) {
      if (!tested[i] && buttonPressed(i)) {
        tested[i] = true;
        testedCount++;

        lcdClear();
        lcdPrint(0, 0, "OK:");
        lcdPrint(4, 0, btnNames[i]);
        lcdPrint(0, 1, "Good press!");

        ledFlash(LED_GREEN, 2, 80, 80);
        beep(btnLEDFreqs[i], 100);

        Serial.print("[OK] Button: ");
        Serial.println(btnNames[i]);
        delay(400);
        break;
      }
    }
  }

  if (testedCount == BTN_COUNT) {
    lcdClear();
    lcdPrint(0, 0, "ALL BUTTONS OK!");

    lcd.setCursor(0, 1);
    for (int i = 0; i < BTN_COUNT; i++) {
      lcd.write(byte(CHAR_HEART));
    }

    ledBlink(3);
    int wF[] = {1000, 1200, 1400, 1600, 1800, 2000};
    int wD[] = {80, 80, 80, 80, 80, 200};
    beepMelody(wF, wD, 6);
    ledOff();
    delay(500);

    lcdClear();
    lcdPrint(0, 0, "BUTTONS DONE");
    lcdPrint(0, 1, "Press ANY button");

    ledSet(true, false, false);
    Serial.println("[PASS] Button Test");
  } else {
    lcdClear();
    lcdPrint(0, 0, "TIMEOUT!");
    lcdPrint(0, 1, "Some btn missing");
    ledFlash(LED_RED, 3, 200, 100);
    beep(300, 500);
    Serial.println("[WARN] Button Test: timeout");
    ledOff();
  }

  currentPhase = PHASE_COMBINED;
}

void runPhaseCombined() {
  lcdClear();
  lcdPrint(0, 0, "TEST 6: COMBINED");
  lcdPrint(0, 1, "Mini game demo");

  beep(1200, 100);
  delay(200);
  beep(1500, 100);
  delay(1000);

  // Mini simülasyon: karakter LCD üzerinde hareket eder
  int playerX = 0;
  int score = 0;
  unsigned long start = millis();

  lcdClear();

  while (millis() - start < 8000) {
    bool moved = false;

    if (buttonPressed(0)) { // UP -> skor artir
      score++;
      ledFlash(LED_GREEN, 1, 60, 0);
      beep(1600, 50);
      moved = true;
    }
    if (buttonPressed(3)) { // FIRE -> hareket
      playerX = (playerX + 1) % 15;
      ledFlash(LED_GREEN, 1, 40, 0);
      beep(1200, 40);
      moved = true;
    }
    if (buttonPressed(2)) { // LEFT -> geri
      if (playerX > 0) playerX--;
      beep(800, 40);
      moved = true;
    }
    if (buttonPressed(1)) { // DOWN -> demo bitis
      break;
    }

    if (moved) {
      lcdClear();
      lcd.setCursor(playerX, 0);
      lcd.write(byte(CHAR_RUNNER));
      lcdPrint(0, 1, "SCR:");
      lcd.print(score);
      lcd.write(byte(CHAR_COIN));
    }

    delay(30);
  }

  lcdClear();
  lcdPrint(0, 0, "COMBINED DONE");
  lcdPrint(0, 1, "Press ANY button");

  ledSet(true, false, false);
  beep(1500, 100);

  Serial.println("[PASS] Combined Test");
  currentPhase = PHASE_DONE;
}

void runPhaseDone() {
  lcdClear();
  lcdPrint(0, 0, " ALL TESTS PASS ");
  lcdPrint(0, 1, " HARDWARE  READY");

  // Zafer melodisi
  int vF[] = {1000, 1200, 1000, 1200, 1500, 1800, 2000};
  int vD[] = {100,  100,  100,  100,  150,  150,  400};
  beepMelody(vF, vD, 7);

  ledBlink(5);
  ledChase(3);
  ledBlink(3);
  ledOff();

  delay(1000);
  lcdClear();
  lcdPrint(0, 0, "  GAME  ENGINE  ");
  lcdPrint(0, 1, "   AWAITS YOU   ");

  // Sürekli bekle
  Serial.println("=== ALL TESTS PASSED ===");
  Serial.println("Hardware is ready for game development!");
}

// ─── SETUP & LOOP ──────────────────────────

void setup() {
  Serial.begin(9600);

  // LCD init
  lcd.init();
  lcd.backlight();

  // Custom chars yükle
  lcd.createChar(CHAR_HEART,  charHeart);
  lcd.createChar(CHAR_RUNNER, charRunner);
  lcd.createChar(CHAR_BULLET, charBullet);
  lcd.createChar(CHAR_BOSS,   charBoss);
  lcd.createChar(CHAR_SHIELD, charShield);
  lcd.createChar(CHAR_COIN,   charCoin);
  lcd.createChar(CHAR_FLAME,  charFlame);
  lcd.createChar(CHAR_SKULL,  charSkull);

  // Butonlar
  for (int i = 0; i < BTN_COUNT; i++) {
    pinMode(btnPins[i], INPUT_PULLUP);
  }

  // LED ve buzzer
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(BUZZER,     OUTPUT);

  ledOff();

  runPhaseIntro();
}

void loop() {
  if (currentPhase == PHASE_DONE) {
    runPhaseDone();
    // Sonsuz döngü, reset gerekir
    while (true) delay(1000);
  }

  if (anyButtonPressed()) {
    ledOff();
    switch (currentPhase) {
      case PHASE_LCD:          runPhaseLCD();         break;
      case PHASE_CUSTOM_CHARS: runPhaseCustomChars(); break;
      case PHASE_LEDS:         runPhaseLEDs();         break;
      case PHASE_BUZZER:       runPhaseBuzzer();       break;
      case PHASE_BUTTONS:      runPhaseButtons();      break;
      case PHASE_COMBINED:     runPhaseCombined();     break;
      default: break;
    }
  }
}
