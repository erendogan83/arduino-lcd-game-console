/*
 * Whack-a-Mole - v2
 * Author  : Eren DOGAN
 * GitHub  : github.com/erendogan83
 *
 * HOLE LAYOUT (16x2):
 *
 *   col:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
 *   row0: .  .  .  .  L  .  .  .  .  U  .  .  .  R  >  N
 *   row1: .  .  .  .  .  .  .  .  .  D  .  .  .  .  S  S
 *
 *   L = hole LEFT   col 2  row 0   button: LEFT (D4)
 *   U = hole UP     col 7  row 0   button: UP   (D2)
 *   D = hole DOWN   col 7  row 1   button: DOWN (D3)  <- same col as U, diff row
 *   R = hole RIGHT  col 13 row 0   button: FIRE (D5)
 *
 *   HUD col 14-15:
 *     row 0: -> N  (lives)
 *     row 1: SS    (score 0-99)
 *
 * CONTROLS:
 *   LEFT (D4) = whack L
 *   UP   (D2) = whack U
 *   DOWN (D3) = whack D
 *   FIRE (D5) = whack R
 *
 * GAMEPLAY:
 *   Moles pop up randomly. Hit the matching button while mole is up -> +1 pt.
 *   Mole escapes (timer expires) -> -1 life.
 *   Score 0-14 : max 1 mole at a time
 *   Score 15+  : max 2 moles at a time
 *   Window shrinks and spawn rate increases with score.
 *   3 lives. EEPROM high score.
 *
 * SPRITES:
 *   0 = hole (empty)
 *   1 = mole (visible, hit this!)
 *   2 = hit  (whacked!)
 *   3 = miss (escaped)
 *
 * PINS: A4,A5=LCD | D2=UP | D3=DOWN | D4=LEFT | D5=FIRE
 *       D6=Buzz | D7=Green | D8=Yellow | D9=Red
 * EEPROM: addr 10-11
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
#define NUM_HOLES 4

// Hole indices
#define HOLE_L 0   // LEFT button
#define HOLE_U 1   // UP button
#define HOLE_D 2   // DOWN button
#define HOLE_R 3   // FIRE button

// Hole screen positions
const int HOLE_COL[NUM_HOLES] = { 1, 6, 6, 10 };
const int HOLE_ROW[NUM_HOLES] = { 0, 0, 1,  0 };

// Mole visible window ms — shrinks with score
const int WIN_BASE = 2000;
const int WIN_MIN  = 650;
int getWindow(int sc){ int w=WIN_BASE-(sc/5)*120; return w<WIN_MIN?WIN_MIN:w; }

// Spawn attempt interval ms — shrinks with score
const int SPAWN_BASE = 1600;
const int SPAWN_MIN  = 550;
int getSpawn(int sc){ int s=SPAWN_BASE-(sc/5)*90; return s<SPAWN_MIN?SPAWN_MIN:s; }

#define FLASH_MS 300

#define EE_MOLE 10
int  eeGet(){ int v=((int)EEPROM.read(EE_MOLE)<<8)|EEPROM.read(EE_MOLE+1); return(v<0||v>9999)?0:v; }
void eePut(int s){ if(s>eeGet()){EEPROM.write(EE_MOLE,(s>>8)&0xFF);EEPROM.write(EE_MOLE+1,s&0xFF);} }

// ── Sprites ───────────────────────────────────────────────────────────────────
byte spHole[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b01110,
  0b11111,
  0b11111
};
byte spMole[8] = {
  0b00000,
  0b01110,
  0b11111,
  0b10101,
  0b11111,
  0b01110,
  0b11111,
  0b11111
};
byte spHit[8] = {
  0b10101,
  0b01110,
  0b11111,
  0b01110,
  0b10101,
  0b01110,
  0b11111,
  0b11111
};
byte spMiss[8] = {
  0b00000,
  0b00000,
  0b10001,
  0b01110,
  0b00000,
  0b01110,
  0b11111,
  0b11111
};
#define S_HOLE 0
#define S_MOLE 1
#define S_HIT  2
#define S_MISS 3

void loadSprites(){
  lcd.createChar(S_HOLE,spHole);
  lcd.createChar(S_MOLE,spMole);
  lcd.createChar(S_HIT, spHit);
  lcd.createChar(S_MISS,spMiss);
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

// ── Buttons — rising edge, per-button lock ────────────────────────────────────
#define BTN_LOCK 120
bool upPrev=false;  unsigned long upLock=0;
bool dnPrev=false;  unsigned long dnLock=0;
bool lePrev=false;  unsigned long leLock=0;
bool fiPrev=false;  unsigned long fiLock=0;

bool upPressed(){
  bool now=(digitalRead(PIN_UP)==LOW); bool edge=(now&&!upPrev); upPrev=now;
  if(edge&&millis()>=upLock){upLock=millis()+BTN_LOCK;return true;} return false;
}
bool downPressed(){
  bool now=(digitalRead(PIN_DOWN)==LOW); bool edge=(now&&!dnPrev); dnPrev=now;
  if(edge&&millis()>=dnLock){dnLock=millis()+BTN_LOCK;return true;} return false;
}
bool leftPressed(){
  bool now=(digitalRead(PIN_LEFT)==LOW); bool edge=(now&&!lePrev); lePrev=now;
  if(edge&&millis()>=leLock){leLock=millis()+BTN_LOCK;return true;} return false;
}
bool firePressed(){
  bool now=(digitalRead(PIN_FIRE)==LOW); bool edge=(now&&!fiPrev); fiPrev=now;
  if(edge&&millis()>=fiLock){fiLock=millis()+BTN_LOCK;return true;} return false;
}
bool anyPressed(){
  return (digitalRead(PIN_UP)==LOW)||(digitalRead(PIN_DOWN)==LOW)||
         (digitalRead(PIN_LEFT)==LOW)||(digitalRead(PIN_FIRE)==LOW);
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

// ── Hole state machine ────────────────────────────────────────────────────────
enum HoleState { EMPTY, MOLE_UP, FLASH_HIT, FLASH_MISS };
HoleState holeState[NUM_HOLES];
unsigned long holeTimer[NUM_HOLES];   // expiry time for current state

// ── Game variables ────────────────────────────────────────────────────────────
int  score, best, lives;
bool gameOver;
unsigned long lastSpawnTick;

enum AppState { ST_MENU, ST_TUTORIAL, ST_GAME, ST_GAMEOVER };
AppState appState = ST_MENU;

// ── Whack a hole ──────────────────────────────────────────────────────────────
void whack(int idx){
  if(holeState[idx]==MOLE_UP){
    holeState[idx]=FLASH_HIT;
    holeTimer[idx]=millis()+FLASH_MS;
    score++;
    tone(PIN_BUZZ,1800,80);
    flashNB(PIN_GREEN,100);
  } else if(holeState[idx]==EMPTY){
    // Wrong hole — dull thud, no penalty
    tone(PIN_BUZZ,350,50);
  }
}

// ── Spawn ─────────────────────────────────────────────────────────────────────
void trySpawn(){
  int empty[NUM_HOLES]; int en=0;
  for(int i=0;i<NUM_HOLES;i++) if(holeState[i]==EMPTY) empty[en++]=i;
  if(en==0) return;

  int active=0;
  for(int i=0;i<NUM_HOLES;i++) if(holeState[i]==MOLE_UP) active++;
  int maxM=(score>=15)?2:1;
  if(active>=maxM) return;

  int pick=empty[random(en)];
  holeState[pick]=MOLE_UP;
  holeTimer[pick]=millis()+getWindow(score);
}

// ── Reset ─────────────────────────────────────────────────────────────────────
void resetGame(){
  score=0; lives=3; gameOver=false; best=eeGet();
  for(int i=0;i<NUM_HOLES;i++){ holeState[i]=EMPTY; holeTimer[i]=0; }
  upPrev=false; dnPrev=false; lePrev=false; fiPrev=false;
  lastSpawnTick=millis();
  bInit(); bClr(); bFlush();
}

// ── Render ────────────────────────────────────────────────────────────────────
void doRender(){
  bClr();

  // Draw holes
  for(int i=0;i<NUM_HOLES;i++){
    int c=HOLE_COL[i], r=HOLE_ROW[i];
    switch(holeState[i]){
      case EMPTY:      bSp(c,r,S_HOLE); break;
      case MOLE_UP:    bSp(c,r,S_MOLE); break;
      case FLASH_HIT:  bSp(c,r,S_HIT);  break;
      case FLASH_MISS: bSp(c,r,S_MISS); break;
    }
  }

  // HUD — col 14-15 only
  bCh(14,0,(char)(0x7e));  // -> arrow
  bCh(15,0,'0'+lives);
  bCh(14,1,'0'+(score/10)%10);
  bCh(15,1,'0'+ score    %10);

  bFlush();
}

// ── Tick ──────────────────────────────────────────────────────────────────────
void doTick(){
  unsigned long now=millis();

  for(int i=0;i<NUM_HOLES;i++){
    if(holeState[i]==MOLE_UP && now>=holeTimer[i]){
      // Escaped
      holeState[i]=FLASH_MISS;
      holeTimer[i]=now+FLASH_MS;
      lives--;
      tone(PIN_BUZZ,250,200);
      flashNB(PIN_RED,250);
      if(lives<=0){
        gameOver=true;
        eePut(score);
        if(score>best) best=score;
        appState=ST_GAMEOVER;
        return;
      }
    }
    if((holeState[i]==FLASH_HIT||holeState[i]==FLASH_MISS)&&now>=holeTimer[i]){
      holeState[i]=EMPTY;
    }
  }

  if(now-lastSpawnTick>=(unsigned long)getSpawn(score)){
    lastSpawnTick=now;
    trySpawn();
  }
}

// ── Game over ─────────────────────────────────────────────────────────────────
void showGameOver(){
  // Force all LEDs off — game may have ended mid-flash
  digitalWrite(PIN_GREEN,  LOW);
  digitalWrite(PIN_YELLOW, LOW);
  digitalWrite(PIN_RED,    LOW);
  flashPin = -1;
  bClr();
  const char g1[]="  GAME  OVER   "; for(int i=0;g1[i];i++) bCh(i,0,g1[i]);
  bCh(0,1,'S');bCh(1,1,'C');bCh(2,1,'R');bCh(3,1,':');
  bCh(4,1,'0'+(score/10)%10);bCh(5,1,'0'+score%10);
  bCh(7,1,'B');bCh(8,1,'S');bCh(9,1,'T');bCh(10,1,':');
  bCh(11,1,'0'+(best/10)%10);bCh(12,1,'0'+best%10);
  bFlush();
  unsigned long t=millis(); while(millis()-t<2000){if(anyPressed())break;delay(10);}

  // New record celebration
  if(score>0 && score==best){
    bClr();
    const char rec[]="* NEW RECORD! *"; for(int i=0;rec[i];i++) bCh(i,0,rec[i]);
    bCh(6,1,'0'+(score/10)%10);
    bCh(7,1,'0'+ score    %10);
    bFlush();
    for(int i=0;i<4;i++){
      digitalWrite(PIN_GREEN,HIGH);
      tone(PIN_BUZZ,2000,80);
      t=millis(); while(millis()-t<200){if(anyPressed())break;delay(5);}
      digitalWrite(PIN_GREEN,LOW);
      t=millis(); while(millis()-t<100){if(anyPressed())break;delay(5);}
    }
    t=millis(); while(millis()-t<500){if(anyPressed())break;delay(10);}
  }

  bClr();
  const char r1[]="ANY to restart"; for(int i=0;r1[i];i++) bCh(i,0,r1[i]);
  const char r2[]="BEST:";          for(int i=0;r2[i];i++) bCh(i,1,r2[i]);
  bCh(6,1,'0'+(best/10)%10);
  bCh(7,1,'0'+ best    %10);
  bFlush();
  while(!anyPressed()) delay(10);
  appState=ST_MENU;
}

// ── Tutorial ──────────────────────────────────────────────────────────────────
void showTutorial(){
  loadSprites(); bInit();
  unsigned long t;

  // Page 1: LEFT hole mapping
  // Show hole L with "= LEFT" label
  bClr();
  bSp(HOLE_COL[HOLE_L],HOLE_ROW[HOLE_L],S_HOLE);
  bSp(HOLE_COL[HOLE_U],HOLE_ROW[HOLE_U],S_HOLE);
  bSp(HOLE_COL[HOLE_D],HOLE_ROW[HOLE_D],S_HOLE);
  bSp(HOLE_COL[HOLE_R],HOLE_ROW[HOLE_R],S_HOLE);
  bCh(3,0,'<');bCh(5,0,'L');bCh(6,0,'E');bCh(7,0,'F');bCh(8,0,'T');
  bFlush();
  t=millis(); while(millis()-t<1800){if(anyPressed())goto page2;delay(10);}

  // Page 2: UP / DOWN hole mapping
  page2:
  bClr();
  bSp(HOLE_COL[HOLE_L],HOLE_ROW[HOLE_L],S_HOLE);
  bSp(HOLE_COL[HOLE_U],HOLE_ROW[HOLE_U],S_HOLE);
  bSp(HOLE_COL[HOLE_D],HOLE_ROW[HOLE_D],S_HOLE);
  bSp(HOLE_COL[HOLE_R],HOLE_ROW[HOLE_R],S_HOLE);
  bCh(8,0,'^');bCh(9,0,'U');bCh(10,0,'P');
  bCh(8,1,'v');bCh(9,1,'D');bCh(10,1,'N');
  bFlush();
  t=millis(); while(millis()-t<1800){if(anyPressed())goto page3;delay(10);}

  // Page 3: RIGHT (FIRE) hole mapping
  page3:
  bClr();
  bSp(HOLE_COL[HOLE_L],HOLE_ROW[HOLE_L],S_HOLE);
  bSp(HOLE_COL[HOLE_U],HOLE_ROW[HOLE_U],S_HOLE);
  bSp(HOLE_COL[HOLE_D],HOLE_ROW[HOLE_D],S_HOLE);
  bSp(HOLE_COL[HOLE_R],HOLE_ROW[HOLE_R],S_HOLE);
  bCh(9,0,'F');bCh(10,0,'I');bCh(11,0,'R');bCh(12,0,'E');bCh(13,0,'>');
  bFlush();
  t=millis(); while(millis()-t<1800){if(anyPressed())goto page4;delay(10);}

  // Page 4: mole pops up demo — whack it!
  page4:
  bClr();
  bSp(HOLE_COL[HOLE_L],HOLE_ROW[HOLE_L],S_MOLE);
  bSp(HOLE_COL[HOLE_U],HOLE_ROW[HOLE_U],S_HOLE);
  bSp(HOLE_COL[HOLE_D],HOLE_ROW[HOLE_D],S_HOLE);
  bSp(HOLE_COL[HOLE_R],HOLE_ROW[HOLE_R],S_HOLE);
  bCh(4,0,'W');bCh(5,0,'H');bCh(6,0,'A');bCh(7,0,'C');bCh(8,0,'K');bCh(9,0,'!');
  bCh(4,1,'P');bCh(5,1,'R');bCh(6,1,'E');bCh(7,1,'S');bCh(8,1,'S');
  bCh(10,1,'L');bCh(11,1,'F');bCh(12,1,'T');
  bFlush();
  t=millis(); while(millis()-t<2000){if(anyPressed())goto page5;delay(10);}

  // Page 5: miss demo — too slow!
  page5:
  bClr();
  bSp(HOLE_COL[HOLE_L],HOLE_ROW[HOLE_L],S_MISS);
  bSp(HOLE_COL[HOLE_U],HOLE_ROW[HOLE_U],S_HOLE);
  bSp(HOLE_COL[HOLE_D],HOLE_ROW[HOLE_D],S_HOLE);
  bSp(HOLE_COL[HOLE_R],HOLE_ROW[HOLE_R],S_HOLE);
  bCh(4,0,'T');bCh(5,0,'O');bCh(6,0,'O');
  bCh(8,0,'S');bCh(9,0,'L');bCh(10,0,'O');bCh(11,0,'W');bCh(12,0,'!');
  bCh(4,1,'-');bCh(5,1,'1');bCh(7,1,'L');bCh(8,1,'I');bCh(9,1,'F');bCh(10,1,'E');
  bFlush();
  tone(PIN_BUZZ,300,200);
  t=millis(); while(millis()-t<2000){if(anyPressed())break;delay(10);}

  // Countdown 3-2-1 GO!
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
  const char m1[]="WHACK  A  MOLE!"; for(int i=0;m1[i];i++) bCh(i,0,m1[i]);
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

    case ST_MENU:
      showMenu();
      break;

    case ST_TUTORIAL:
      showTutorial();
      break;

    case ST_GAME:
      if(gameOver){ appState=ST_GAMEOVER; break; }

      // Buttons map directly to holes — intuitive layout
      if(leftPressed())  whack(HOLE_L);
      if(upPressed())    whack(HOLE_U);
      if(downPressed())  whack(HOLE_D);
      if(firePressed())  whack(HOLE_R);

      doTick();
      doRender();
      flashUpdate();
      break;

    case ST_GAMEOVER:
      showGameOver();
      break;
  }
}
