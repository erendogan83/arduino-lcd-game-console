/*
 * Reaction Game - v3
 * Author  : Eren DOGAN
 * GitHub  : github.com/erendogan83
 *
 * RULES (simple and final):
 *   - Target spawns in row 0, cols 2-13
 *   - Marker bounces left-right in row 1
 *   - Press FIRE when markerCol == targetCol -> HIT -> +10 pts
 *   - Press FIRE any other time             -> MISS -> -1 life
 *   - 3 combo -> x2 multiplier, 5 combo -> x3
 *   - Second target unlocks at score 30
 *
 * SCREEN:
 *   Row 0: [  targets spawn here cols 2-13  ] [♥N]   col 14-15 = lives
 *   Row 1: [SCR] [  marker bounces  ]                col 0-3   = score
 *
 * LOOP ORDER (never changes):
 *   1. Move marker if tick elapsed
 *   2. Blink targets if tick elapsed
 *   3. Read FIRE input  <- always sees current markerCol
 *   4. flashUpdate
 *
 * DEBOUNCE:
 *   After any FIRE press (hit or miss) button is locked for 250ms.
 *   No delay() anywhere in gameplay.
 *
 * CONTROLS: FIRE(D5) | UP(D2) LEFT(D4) = menu
 * PINS: A4,A5=LCD D2=UP D4=LEFT D5=FIRE
 *       D6=Buzz D7=Green D8=Yellow D9=Red
 * EEPROM: addr 8-9
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27,16,2);

#define PIN_UP     2
#define PIN_LEFT   4
#define PIN_FIRE   5
#define PIN_BUZZ   6
#define PIN_GREEN  7
#define PIN_YELLOW 8
#define PIN_RED    9

#define COLS        16
#define ROWS        2
#define TARGET_MIN  2    // leftmost col a target can spawn
#define TARGET_MAX  13   // rightmost col a target can spawn

// Speed: ms per marker step. 300ms start, 80ms floor, -8ms per 5 pts.
const int SPD_BASE = 300;
const int SPD_MIN  = 80;
int getSpd(int sc){ int s=SPD_BASE-(sc/5)*8; return s<SPD_MIN?SPD_MIN:s; }

// Blink interval: 700ms start, 200ms floor, -20ms per 5 pts.
const int BLK_BASE = 700;
const int BLK_MIN  = 200;
int getBlk(int sc){ int s=BLK_BASE-(sc/5)*20; return s<BLK_MIN?BLK_MIN:s; }

#define EE_REACT 8
int  eeGet(){ int v=((int)EEPROM.read(EE_REACT)<<8)|EEPROM.read(EE_REACT+1); return(v<0||v>9999)?0:v; }
void eePut(int s){ if(s>eeGet()){EEPROM.write(EE_REACT,(s>>8)&0xFF);EEPROM.write(EE_REACT+1,s&0xFF);} }

// ── Sprites ───────────────────────────────────────────────────────────────────
byte spTarget[8] = {0b00100,0b01110,0b11111,0b11111,0b11111,0b01110,0b00100,0b00000};
byte spMarker[8] = {0b00000,0b00100,0b01110,0b11111,0b01110,0b00100,0b00000,0b00000};
byte spHit[8]    = {0b10101,0b01110,0b11111,0b11111,0b11111,0b01110,0b10101,0b00000};
byte spMiss[8]   = {0b10001,0b11011,0b01110,0b00100,0b01110,0b11011,0b10001,0b00000};
#define S_TARGET 0
#define S_MARKER 1
#define S_HIT    2
#define S_MISS   3

void loadSprites(){
  lcd.createChar(S_TARGET,spTarget);
  lcd.createChar(S_MARKER,spMarker);
  lcd.createChar(S_HIT,   spHit);
  lcd.createChar(S_MISS,  spMiss);
}

// ── Double buffer ─────────────────────────────────────────────────────────────
char cur[ROWS][COLS], prv[ROWS][COLS];
void bClr(){ for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) cur[r][c]=' '; }
void bCh(int c,int r,char ch){ if(c>=0&&c<COLS&&r>=0&&r<ROWS) cur[r][c]=ch; }
void bSp(int c,int r,byte s) { if(c>=0&&c<COLS&&r>=0&&r<ROWS) cur[r][c]=(char)(128+s); }
void bFlush(){
  for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++){
    if(cur[r][c]!=prv[r][c]){
      lcd.setCursor(c,r);
      uint8_t v=(uint8_t)cur[r][c];
      if(v>=128) lcd.write(v-128); else lcd.write(v);
      prv[r][c]=cur[r][c];
    }
  }
}
void bInit(){ for(int r=0;r<ROWS;r++) for(int c=0;c<COLS;c++) prv[r][c]='\0'; }

// ── Button ────────────────────────────────────────────────────────────────────
// Single debounce variable shared for FIRE.
// After any press (hit or miss) lockUntil is set 250ms into the future.
// firePressed() returns true only once per physical press.
unsigned long fireLockUntil = 0;
bool          firePrev      = false;

bool firePressed(){
  bool now = (digitalRead(PIN_FIRE)==LOW);
  bool edge = (now && !firePrev);   // rising edge (press, not hold)
  firePrev = now;
  if(edge && millis() >= fireLockUntil) return true;
  return false;
}

void lockFire(){ fireLockUntil = millis() + 250; }

bool anyPressed(){
  return (digitalRead(PIN_UP)   ==LOW)||
         (digitalRead(PIN_LEFT) ==LOW)||
         (digitalRead(PIN_FIRE) ==LOW);
}

// ── LED flash (non-blocking) ──────────────────────────────────────────────────
int           flashPin = -1;
unsigned long flashEnd = 0;
void flashNB(int pin, int ms){
  if(flashPin>=0 && flashPin!=pin) digitalWrite(flashPin,LOW);
  digitalWrite(pin,HIGH);
  flashPin = pin;
  flashEnd = millis()+ms;
}
void flashUpdate(){
  if(flashPin>=0 && millis()>=flashEnd){ digitalWrite(flashPin,LOW); flashPin=-1; }
}

// ── Game variables ────────────────────────────────────────────────────────────
int  markerCol;
int  markerDir;

// Two target slots
int  tCol[2];
bool tActive[2];
bool tBlink[2];   // true=visible, false=hidden

int  score, best, lives, combo, multiplier;
bool gameOver;

unsigned long lastMarkerTick;
unsigned long lastBlinkTick;
int  markerSpd;
int  blinkSpd;

// Brief on-screen flash after hit/miss (non-blocking)
bool          showFlash;
unsigned long flashMsgEnd;
bool          flashIsHit;
int           flashCol;

enum AppState { ST_MENU, ST_TUTORIAL, ST_GAME, ST_GAMEOVER };
AppState appState = ST_MENU;

// ── Spawn target ──────────────────────────────────────────────────────────────
void spawnTarget(int slot){
  // Valid cols: TARGET_MIN..TARGET_MAX, not marker, not other target
  int valid[COLS];
  int vn = 0;
  for(int c=TARGET_MIN; c<=TARGET_MAX; c++){
    if(c==markerCol) continue;
    if(tActive[1-slot] && c==tCol[1-slot]) continue;
    valid[vn++] = c;
  }
  tCol[slot]    = (vn>0) ? valid[random(vn)] : TARGET_MIN+random(TARGET_MAX-TARGET_MIN+1);
  tActive[slot] = true;
  tBlink[slot]  = true;
}

// ── Reset ─────────────────────────────────────────────────────────────────────
void resetGame(){
  markerCol  = TARGET_MIN;
  markerDir  = 1;
  score      = 0;
  lives      = 3;
  combo      = 0;
  multiplier = 1;
  gameOver   = false;
  best       = eeGet();
  markerSpd  = getSpd(0);
  blinkSpd   = getBlk(0);
  showFlash  = false;
  tActive[0] = false;
  tActive[1] = false;
  firePrev       = false;
  fireLockUntil  = 0;
  spawnTarget(0);
  lastMarkerTick = millis();
  lastBlinkTick  = millis();
  bInit(); bClr(); bFlush();
}

// ── Render ────────────────────────────────────────────────────────────────────
void doRender(){
  bClr();

  // Row 0: targets (blink visible only)
  for(int s=0;s<2;s++)
    if(tActive[s] && tBlink[s])
      bSp(tCol[s], 0, S_TARGET);

  // Row 0: lives HUD — col 14-15, never conflicts with target zone 2-13
  bCh(14, 0, (char)(0x7e));   // -> arrow = lives label
  bCh(15, 0, '0'+lives);

  // Row 1: score — col 0-3
  bCh(0,1,'0'+(score/100)%10);
  bCh(1,1,'0'+(score/ 10)%10);
  bCh(2,1,'0'+ score     %10);

  // Row 1: combo multiplier — col 4-5 (only when active)
  if(multiplier>1){ bCh(4,1,'x'); bCh(5,1,'0'+multiplier); }

  // Row 1: marker — drawn last, always visible
  bSp(markerCol, 1, S_MARKER);

  // Row 1: brief hit/miss sprite (non-blocking)
  if(showFlash && millis()<flashMsgEnd){
    bSp(flashCol, 1, flashIsHit ? S_HIT : S_MISS);
  } else {
    showFlash = false;
  }

  bFlush();
}

// ── Handle FIRE ───────────────────────────────────────────────────────────────
void handleFire(){
  // Lock button immediately — no second trigger from same press
  lockFire();

  // Check exact column match
  int hitSlot = -1;
  for(int s=0;s<2;s++){
    if(tActive[s] && markerCol==tCol[s]){ hitSlot=s; break; }
  }

  flashCol = markerCol;

  if(hitSlot >= 0){
    // HIT
    combo++;
    multiplier = (combo>=5)?3:(combo>=3)?2:1;
    score += 10*multiplier;

    tone(PIN_BUZZ, 1800, 100);
    flashNB(PIN_GREEN, 120);

    tActive[hitSlot] = false;
    spawnTarget(hitSlot);
    if(score>=30 && !tActive[1]) spawnTarget(1);

    markerSpd = getSpd(score);
    blinkSpd  = getBlk(score);

    flashIsHit  = true;
    showFlash   = true;
    flashMsgEnd = millis()+300;

  } else {
    // MISS
    combo = 0; multiplier = 1;
    lives--;

    tone(PIN_BUZZ, 250, 300);
    flashNB(PIN_RED, 300);

    // Reset marker tick so it doesn't jump after miss tone
    lastMarkerTick = millis();

    flashIsHit  = false;
    showFlash   = true;
    flashMsgEnd = millis()+300;

    if(lives<=0){
      gameOver = true;
      eePut(score);
      if(score>best) best=score;
      appState = ST_GAMEOVER;
    }
  }
}

// ── Game over ─────────────────────────────────────────────────────────────────
void showGameOver(){
  bClr();
  const char g1[]="  GAME  OVER   "; for(int i=0;g1[i];i++) bCh(i,0,g1[i]);
  bCh(0,1,'S');bCh(1,1,'C');bCh(2,1,'R');bCh(3,1,':');
  bCh(4,1,'0'+(score/100)%10);bCh(5,1,'0'+(score/10)%10);bCh(6,1,'0'+score%10);
  bCh(8,1,'B');bCh(9,1,'S');bCh(10,1,'T');bCh(11,1,':');
  bCh(12,1,'0'+(best/100)%10);bCh(13,1,'0'+(best/10)%10);bCh(14,1,'0'+best%10);
  bFlush();
  unsigned long t=millis(); while(millis()-t<2000){if(anyPressed())break;delay(10);}

  bClr();
  const char r1[]="ANY to restart"; for(int i=0;r1[i];i++) bCh(i,0,r1[i]);
  const char r2[]="BEST:";          for(int i=0;r2[i];i++) bCh(i,1,r2[i]);
  bCh(6,1,'0'+(best/100)%10);
  bCh(7,1,'0'+(best/ 10)%10);
  bCh(8,1,'0'+ best     %10);
  bFlush();
  while(!anyPressed()) delay(10);
  appState=ST_MENU;
}

// ── Tutorial ──────────────────────────────────────────────────────────────────
void showTutorial(){
  loadSprites(); bInit(); bClr();

  // Page 1: marker bouncing demo
  for(int pass=0;pass<2;pass++){
    for(int c=0;c<COLS;c++){
      bClr();
      bCh(0,0,'M');bCh(1,0,'A');bCh(2,0,'R');bCh(3,0,'K');bCh(4,0,'E');bCh(5,0,'R');
      bCh(7,0,'M');bCh(8,0,'O');bCh(9,0,'V');bCh(10,0,'E');bCh(11,0,'S');
      bSp(c,1,S_MARKER);
      bFlush();
      unsigned long t=millis();
      while(millis()-t<80){ if(anyPressed()) goto page2; delay(5); }
    }
    for(int c=COLS-1;c>=0;c--){
      bClr();
      bCh(0,0,'M');bCh(1,0,'A');bCh(2,0,'R');bCh(3,0,'K');bCh(4,0,'E');bCh(5,0,'R');
      bCh(7,0,'M');bCh(8,0,'O');bCh(9,0,'V');bCh(10,0,'E');bCh(11,0,'S');
      bSp(c,1,S_MARKER);
      bFlush();
      unsigned long t=millis();
      while(millis()-t<80){ if(anyPressed()) goto page2; delay(5); }
    }
  }

  page2:
  // Page 2: target at col 8, marker approaches, hit demo
  for(int c=0;c<=8;c++){
    bClr();
    bCh(0,0,'H');bCh(1,0,'I');bCh(2,0,'T');bCh(4,0,'T');bCh(5,0,'A');
    bCh(6,0,'R');bCh(7,0,'G');bCh(8,0,'E');bCh(9,0,'T');bCh(10,0,'!');
    bSp(8,0,S_TARGET);
    bSp(c,1,S_MARKER);
    bCh(12,1,'F');bCh(13,1,'I');bCh(14,1,'R');bCh(15,1,'E');
    bFlush();
    unsigned long t=millis();
    while(millis()-t<130){ if(anyPressed()) goto countdown; delay(5); }
  }
  bClr();
  bSp(8,0,S_HIT); bSp(8,1,S_MARKER);
  bCh(10,1,'+');bCh(11,1,'1');bCh(12,1,'0');
  bFlush();
  tone(PIN_BUZZ,1800,120);
  unsigned long tw=millis(); while(millis()-tw<700){if(anyPressed())break;delay(10);}

  countdown:
  for(int i=3;i>=1;i--){
    bClr();
    bCh(7,0,'0'+i);
    bCh(5,1,'R');bCh(6,1,'E');bCh(7,1,'A');
    bCh(8,1,'D');bCh(9,1,'Y');bCh(10,1,'!');
    bFlush();
    tone(PIN_BUZZ, i==1?1200:1000, 80);
    unsigned long t=millis(); while(millis()-t<900){if(anyPressed())break;delay(10);}
  }
  bClr();
  bCh(5,0,'G');bCh(6,0,'O');bCh(7,0,'!');
  bFlush();
  tone(PIN_BUZZ,1600,60); delay(70); tone(PIN_BUZZ,2000,120); delay(400);

  resetGame();
  appState=ST_GAME;
}

// ── Menu ──────────────────────────────────────────────────────────────────────
void showMenu(){
  loadSprites(); bInit(); bClr();
  const char m1[]="REACTION  GAME!"; for(int i=0;m1[i];i++) bCh(i,0,m1[i]);
  const char m2[]="FIRE on target!"; for(int i=0;m2[i];i++) bCh(i,1,m2[i]);
  bFlush();
  bool bl=true; unsigned long lb=millis();
  while(!anyPressed()){
    if(millis()-lb>500){lb=millis();bl=!bl;bCh(0,0,bl?'>':' ');bFlush();}
    delay(10);
  }
  tone(PIN_BUZZ,1500,80);
  appState=ST_TUTORIAL;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup(){
  lcd.init(); lcd.backlight(); loadSprites();
  pinMode(PIN_UP,    INPUT_PULLUP);
  pinMode(PIN_LEFT,  INPUT_PULLUP);
  pinMode(PIN_FIRE,  INPUT_PULLUP);
  pinMode(PIN_BUZZ,  OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_YELLOW,OUTPUT);
  pinMode(PIN_RED,   OUTPUT);
  randomSeed(analogRead(A0));
  bInit(); lcd.clear();
  appState=ST_MENU;
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop(){
  switch(appState){

    case ST_MENU:
      showMenu();
      break;

    case ST_TUTORIAL:
      showTutorial();
      break;

    case ST_GAME:
      if(gameOver){ appState=ST_GAMEOVER; break; }

      // 1. Move marker
      if(millis()-lastMarkerTick >= (unsigned long)markerSpd){
        lastMarkerTick = millis();
        markerCol += markerDir;
        if(markerCol >= COLS){ markerCol=COLS-1; markerDir=-1; }
        if(markerCol <  0)   { markerCol=0;      markerDir= 1; }
        doRender();
      }

      // 2. Blink targets
      if(millis()-lastBlinkTick >= (unsigned long)blinkSpd){
        lastBlinkTick = millis();
        for(int s=0;s<2;s++) if(tActive[s]) tBlink[s]=!tBlink[s];
        doRender();
      }

      // 3. Read FIRE — sees current markerCol after step 1
      if(firePressed()) handleFire();

      // 4. LED flash
      flashUpdate();
      break;

    case ST_GAMEOVER:
      showGameOver();
      break;
  }
}
