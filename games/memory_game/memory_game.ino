/*
 * Memory Game - v1
 * Author  : Eren DOGAN
 * GitHub  : github.com/erendogan83
 *
 * CONCEPT (Simon Says style):
 *   A sequence of buttons is shown on screen one by one.
 *   Player must repeat the exact sequence in order.
 *   Each round adds one more step to the sequence.
 *   Wrong input = game over.
 *
 * SEQUENCE DISPLAY:
 *   Each step lights up one of 4 symbols on screen:
 *     L = LEFT button  (col 2,  row 0)
 *     U = UP button    (col 6,  row 0)
 *     D = DOWN button  (col 9,  row 1)
 *     R = FIRE button  (col 13, row 0)
 *   Symbol flashes bright when shown, dims when idle.
 *
 * SCORING:
 *   +1 point per successfully completed round.
 *   Speed of display increases every 3 rounds.
 *   Max sequence length: 16 steps.
 *
 * CONTROLS:
 *   LEFT (D4) = L
 *   UP   (D2) = U
 *   DOWN (D3) = D
 *   FIRE (D5) = R
 *
 * PINS: A4,A5=LCD | D2=UP | D3=DOWN | D4=LEFT | D5=FIRE
 *       D6=Buzz | D7=Green | D8=Yellow | D9=Red
 * EEPROM: addr 12-13
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27,16,2);

#define PIN_UP    2
#define PIN_DOWN  3
#define PIN_LEFT  4
#define PIN_FIRE  5
#define PIN_BUZZ  6
#define PIN_GREEN 7
#define PIN_YELLOW 8
#define PIN_RED   9

#define COLS 16
#define ROWS 2

#define MAX_SEQ 16

// Display speed: ms each step is shown during playback. Faster each 3 rounds.
const int SHOW_BASE = 700;
const int SHOW_MIN  = 250;
int getShowMs(int round){ int s=SHOW_BASE-(round/3)*60; return s<SHOW_MIN?SHOW_MIN:s; }

// Gap between steps during playback
const int GAP_BASE = 250;
const int GAP_MIN  = 100;
int getGapMs(int round){ int g=GAP_BASE-(round/3)*20; return g<GAP_MIN?GAP_MIN:g; }

// Player input timeout per step: ms
#define INPUT_TIMEOUT 4000UL

#define EE_MEM 12
int  eeGet(){ int v=((int)EEPROM.read(EE_MEM)<<8)|EEPROM.read(EE_MEM+1); return(v<0||v>9999)?0:v; }
void eePut(int s){ if(s>eeGet()){EEPROM.write(EE_MEM,(s>>8)&0xFF);EEPROM.write(EE_MEM+1,s&0xFF);} }

// ── Sprites ───────────────────────────────────────────────────────────────────
// 4 button symbols — dim versions shown always, bright when active
// Slot 0: L bright   1: U bright   2: D bright   3: R bright
// Dim symbols drawn as ASCII chars to save sprite slots

byte spL[8] = {0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111,0b00000}; // L shape
byte spU[8] = {0b00100,0b01110,0b11111,0b00100,0b00100,0b00100,0b00100,0b00000}; // up arrow
byte spD[8] = {0b00100,0b00100,0b00100,0b00100,0b11111,0b01110,0b00100,0b00000}; // down arrow
byte spR[8] = {0b11110,0b10001,0b10001,0b11110,0b10010,0b10001,0b10001,0b00000}; // R shape

#define S_L 0
#define S_U 1
#define S_D 2
#define S_R 3

void loadSprites(){
  lcd.createChar(S_L,spL);
  lcd.createChar(S_U,spU);
  lcd.createChar(S_D,spD);
  lcd.createChar(S_R,spR);
}

// Symbol screen positions [col, row]
const int SYM_COL[4] = { 2, 6, 9, 13 };
const int SYM_ROW[4] = { 0, 0, 1,  0 };
// Button tones — each symbol has a distinct pitch
const int SYM_TONE[4] = { 523, 659, 784, 1047 }; // C5 E5 G5 C6

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

// ── Buttons — rising edge ─────────────────────────────────────────────────────
#define BTN_LOCK 80
bool upPrev=false;  unsigned long upLock=0;
bool dnPrev=false;  unsigned long dnLock=0;
bool lePrev=false;  unsigned long leLock=0;
bool fiPrev=false;  unsigned long fiLock=0;

bool upPressed(){
  bool now=(digitalRead(PIN_UP)==LOW); bool e=(now&&!upPrev); upPrev=now;
  if(e&&millis()>=upLock){upLock=millis()+BTN_LOCK;return true;} return false;
}
bool downPressed(){
  bool now=(digitalRead(PIN_DOWN)==LOW); bool e=(now&&!dnPrev); dnPrev=now;
  if(e&&millis()>=dnLock){dnLock=millis()+BTN_LOCK;return true;} return false;
}
bool leftPressed(){
  bool now=(digitalRead(PIN_LEFT)==LOW); bool e=(now&&!lePrev); lePrev=now;
  if(e&&millis()>=leLock){leLock=millis()+BTN_LOCK;return true;} return false;
}
bool firePressed(){
  bool now=(digitalRead(PIN_FIRE)==LOW); bool e=(now&&!fiPrev); fiPrev=now;
  if(e&&millis()>=fiLock){fiLock=millis()+BTN_LOCK;return true;} return false;
}
bool anyPressed(){
  return (digitalRead(PIN_UP)==LOW)||(digitalRead(PIN_DOWN)==LOW)||
         (digitalRead(PIN_LEFT)==LOW)||(digitalRead(PIN_FIRE)==LOW);
}

// Returns pressed button index (0=L,1=U,2=D,3=R) or -1
int readButton(){
  if(leftPressed()) return 0;
  if(upPressed())   return 1;
  if(downPressed()) return 2;
  if(firePressed()) return 3;
  return -1;
}

// ── LED flash non-blocking ────────────────────────────────────────────────────
int           flashPin=-1;
unsigned long flashEnd=0;
void flashNB(int pin,int ms){
  if(flashPin>=0&&flashPin!=pin) digitalWrite(flashPin,LOW);
  digitalWrite(pin,HIGH); flashPin=pin; flashEnd=millis()+ms;
}
void flashUpdate(){
  if(flashPin>=0&&millis()>=flashEnd){ digitalWrite(flashPin,LOW); flashPin=-1; }
}
void ledsOff(){
  digitalWrite(PIN_GREEN,LOW);
  digitalWrite(PIN_YELLOW,LOW);
  digitalWrite(PIN_RED,LOW);
  flashPin=-1;
}

// ── Game variables ────────────────────────────────────────────────────────────
byte sequence[MAX_SEQ];   // generated sequence (0-3)
int  seqLen;              // current sequence length
int  score, best;
bool gameOver;

enum AppState { ST_MENU, ST_TUTORIAL, ST_GAME, ST_GAMEOVER };
AppState appState = ST_MENU;

// ── Draw base screen — all 4 symbols dim ─────────────────────────────────────
void drawBase(int highlightIdx, int round){
  bClr();
  for(int i=0;i<4;i++){
    if(i==highlightIdx)
      bSp(SYM_COL[i], SYM_ROW[i], (byte)i);   // bright sprite
    else
      bCh(SYM_COL[i], SYM_ROW[i], '.');        // dim dot
  }
  // Round counter right side
  bCh(14,0,'R'); bCh(15,0,'0'+(round%10));
  // Score bottom right
  bCh(14,1,'0'+(score/10)%10);
  bCh(15,1,'0'+ score    %10);
  bFlush();
}

// ── Flash one symbol with tone (during playback) ──────────────────────────────
void flashSymbol(int idx, int showMs){
  drawBase(idx, seqLen);
  tone(PIN_BUZZ, SYM_TONE[idx], showMs - 40);
  flashNB(PIN_YELLOW, showMs - 40);
  unsigned long t=millis();
  while(millis()-t < (unsigned long)showMs) delay(5);
  drawBase(-1, seqLen);
  flashNB(PIN_YELLOW, 0); // cancel
  digitalWrite(PIN_YELLOW, LOW);
}

// ── Playback sequence to player ───────────────────────────────────────────────
void playSequence(int round){
  int showMs = getShowMs(round);
  int gapMs  = getGapMs(round);
  for(int i=0;i<seqLen;i++){
    flashSymbol(sequence[i], showMs);
    unsigned long t=millis(); while(millis()-t<(unsigned long)gapMs) delay(5);
  }
}

// ── Get player input for one step — returns index or -1 on timeout/wrong ─────
// Returns the button pressed, or -1 on timeout.
int getPlayerInput(){
  upPrev=false; dnPrev=false; lePrev=false; fiPrev=false;
  unsigned long deadline = millis() + INPUT_TIMEOUT;
  while(millis() < deadline){
    int b = readButton();
    if(b >= 0) return b;
    delay(5);
  }
  return -1;  // timeout
}

// ── Reset ─────────────────────────────────────────────────────────────────────
void resetGame(){
  seqLen=0; score=0; gameOver=false; best=eeGet();
  upPrev=false; dnPrev=false; lePrev=false; fiPrev=false;
  bInit(); bClr(); bFlush();
}

// ── Main game flow (blocking per round — suits this game style) ───────────────
void runGame(){
  while(!gameOver){
    // Add one new random step
    if(seqLen < MAX_SEQ) sequence[seqLen++] = random(4);

    // Brief "watch" prompt
    bClr();
    const char w[]="  WATCH...     "; for(int i=0;w[i];i++) bCh(i,0,w[i]);
    bCh(14,0,'R'); bCh(15,0,'0'+(seqLen%10));
    bFlush();
    unsigned long t=millis(); while(millis()-t<800) delay(5);

    // Play sequence
    playSequence(seqLen);

    // Brief "your turn" prompt
    bClr();
    const char y[]="  YOUR TURN!   "; for(int i=0;y[i];i++) bCh(i,0,y[i]);
    bCh(14,0,'R'); bCh(15,0,'0'+(seqLen%10));
    bFlush();
    t=millis(); while(millis()-t<600) delay(5);

    // Player input phase
    bool correct = true;
    for(int i=0;i<seqLen && correct;i++){
      drawBase(-1, seqLen);

      int pressed = getPlayerInput();

      if(pressed < 0){
        // Timeout
        correct = false;
        break;
      }

      // Flash pressed symbol
      drawBase(pressed, seqLen);
      tone(PIN_BUZZ, SYM_TONE[pressed], 120);
      t=millis(); while(millis()-t<150) delay(5);
      drawBase(-1, seqLen);

      if(pressed != sequence[i]){
        correct = false;
      }
    }

    if(correct){
      // Round complete
      score++;
      tone(PIN_BUZZ,1600,60); delay(80); tone(PIN_BUZZ,2000,100);
      flashNB(PIN_GREEN, 200);
      bClr();
      const char g[]="  CORRECT! +1  "; for(int i=0;g[i];i++) bCh(i,0,g[i]);
      bCh(14,1,'0'+(score/10)%10); bCh(15,1,'0'+score%10);
      bFlush();
      t=millis(); while(millis()-t<800) delay(5);
      ledsOff();
    } else {
      // Wrong or timeout
      gameOver = true;
      eePut(score);
      if(score>best) best=score;
      ledsOff();
      // Show wrong symbol briefly
      bClr();
      const char w2[]="  WRONG!       "; for(int w2i=0;w2[w2i];w2i++) bCh(w2i,0,w2[w2i]);
      bFlush();
      tone(PIN_BUZZ,200,500);
      flashNB(PIN_RED,400);
      t=millis(); while(millis()-t<800) delay(5);
      ledsOff();
      appState=ST_GAMEOVER;
    }
  }
}

// ── Game over ─────────────────────────────────────────────────────────────────
void showGameOver(){
  ledsOff();
  bClr();
  const char g1[]="  GAME  OVER   "; for(int i=0;g1[i];i++) bCh(i,0,g1[i]);
  bCh(0,1,'S');bCh(1,1,'C');bCh(2,1,'R');bCh(3,1,':');
  bCh(4,1,'0'+(score/10)%10);bCh(5,1,'0'+score%10);
  bCh(7,1,'B');bCh(8,1,'S');bCh(9,1,'T');bCh(10,1,':');
  bCh(11,1,'0'+(best/10)%10);bCh(12,1,'0'+best%10);
  bFlush();
  unsigned long t=millis(); while(millis()-t<2000){if(anyPressed())break;delay(10);}

  // New record
  if(score>0 && score==best){
    bClr();
    const char rec[]="* NEW RECORD! *"; for(int i=0;rec[i];i++) bCh(i,0,rec[i]);
    bCh(6,1,'0'+(score/10)%10); bCh(7,1,'0'+score%10);
    bFlush();
    for(int i=0;i<4;i++){
      digitalWrite(PIN_GREEN,HIGH); tone(PIN_BUZZ,2000,80);
      t=millis(); while(millis()-t<200){if(anyPressed())break;delay(5);}
      digitalWrite(PIN_GREEN,LOW);
      t=millis(); while(millis()-t<100){if(anyPressed())break;delay(5);}
    }
    t=millis(); while(millis()-t<400){if(anyPressed())break;delay(10);}
  }

  bClr();
  const char r1[]="ANY to restart"; for(int i=0;r1[i];i++) bCh(i,0,r1[i]);
  const char r2[]="BEST:";          for(int i=0;r2[i];i++) bCh(i,1,r2[i]);
  bCh(6,1,'0'+(best/10)%10); bCh(7,1,'0'+best%10);
  bFlush();
  while(!anyPressed()) delay(10);
  appState=ST_MENU;
}

// ── Tutorial ──────────────────────────────────────────────────────────────────
void showTutorial(){
  loadSprites(); bInit();
  unsigned long t;

  // Page 1: show all 4 symbols with button names
  bClr();
  bSp(SYM_COL[0],SYM_ROW[0],S_L); bCh(SYM_COL[0]+1,SYM_ROW[0],'<');
  bSp(SYM_COL[1],SYM_ROW[1],S_U); bCh(SYM_COL[1]+1,SYM_ROW[1],'^');
  bSp(SYM_COL[2],SYM_ROW[2],S_D); bCh(SYM_COL[2]+1,SYM_ROW[2],'v');
  bSp(SYM_COL[3],SYM_ROW[3],S_R); bCh(SYM_COL[3]+1,SYM_ROW[3],'>');
  bFlush();
  t=millis(); while(millis()-t<2200){if(anyPressed())goto page2;delay(10);}

  // Page 2: watch the sequence
  page2:
  bClr();
  const char p2[]="WATCH SEQUENCE!"; for(int i=0;p2[i];i++) bCh(i,0,p2[i]);
  const char p2b[]="THEN REPEAT IT!"; for(int i=0;p2b[i];i++) bCh(i,1,p2b[i]);
  bFlush();
  t=millis(); while(millis()-t<2200){if(anyPressed())goto page3;delay(10);}

  // Page 3: demo — show a short sequence
  page3:
  // Demo sequence: L U R
  byte demo[3]={0,1,3};
  bClr();
  const char p3[]="  DEMO:        "; for(int i=0;p3[i];i++) bCh(i,0,p3[i]);
  bFlush();
  t=millis(); while(millis()-t<600) delay(5);
  for(int i=0;i<3;i++){
    drawBase(demo[i], 3);
    tone(PIN_BUZZ, SYM_TONE[demo[i]], 400);
    t=millis(); while(millis()-t<500) delay(5);
    drawBase(-1, 3);
    t=millis(); while(millis()-t<200) delay(5);
  }
  bClr();
  const char p3b[]=" YOUR TURN NOW!"; for(int i=0;p3b[i];i++) bCh(i,0,p3b[i]);
  bFlush();
  t=millis(); while(millis()-t<1500){if(anyPressed())break;delay(10);}

  // Countdown
  for(int i=3;i>=1;i--){
    bClr();
    bCh(7,0,'0'+i);
    bCh(5,1,'R');bCh(6,1,'E');bCh(7,1,'A');
    bCh(8,1,'D');bCh(9,1,'Y');bCh(10,1,'!');
    bFlush();
    tone(PIN_BUZZ,i==1?1200:1000,80);
    t=millis(); while(millis()-t<900){if(anyPressed())break;delay(10);}
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
  const char m1[]=" MEMORY   GAME! "; for(int i=0;m1[i];i++) bCh(i,0,m1[i]);
  const char m2[]=" ANY to start! "; for(int i=0;m2[i];i++) bCh(i,1,m2[i]);
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
  pinMode(PIN_DOWN,  INPUT_PULLUP);
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
    case ST_MENU:     showMenu();     break;
    case ST_TUTORIAL: showTutorial(); break;
    case ST_GAME:     runGame();      break;
    case ST_GAMEOVER: showGameOver(); break;
  }
}
