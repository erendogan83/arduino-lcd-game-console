/*
 * Snake — v2
 * Author  : Eren DOGAN
 * GitHub  : github.com/erendogan83
 *
 * CONTROLS:
 *   UP   (D2) = move up
 *   DOWN (D3) = move down
 *   LEFT (D4) = move left
 *   FIRE (D5) = move right
 *
 * FEATURES:
 * - 16x2 grid (32 cells), wrap-around walls
 * - Self-collision with tail-tip exclusion (tight U-turns allowed)
 * - 180-degree reversal prevention with 2-input direction queue
 * - Speed: 420ms -> 120ms, drops 10ms every 3 pts
 * - TURBO YEM: snake length >= 12 oldugunda ozel yem cikiyor
 *     Yakalarsan: +5 puan, yilan 4 birim kisalir, alan acilir
 *     Yakalamazsan: 8 saniye sonra kayboluyor, normal devam
 * - Turbo yem sprite: slot 3 (S_DEAD) oyun icinde yeniden kullaniliyor
 *     Olum aninda loadSprites() tekrar cagrilip orijinal sprite geri yukleniyor
 * - Normal yem her zaman aktif; turbo yem ona ek olarak cikiyor
 * - EEPROM high score (addr 2-3, Endless Runner addr 0-1'i kullanir)
 * - Double-buffer rendering, sifir flicker
 * - Non-blocking LED flash
 *
 * PINS: A4,A5=LCD | D2=UP | D3=DOWN | D4=LEFT | D5=RIGHT
 *       D6=Buzz | D7=Green | D8=Yellow | D9=Red
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

#define PIN_UP     2
#define PIN_DOWN   3
#define PIN_LEFT   4
#define PIN_RIGHT  5
#define PIN_BUZZ   6
#define PIN_GREEN  7
#define PIN_YELLOW 8
#define PIN_RED    9

#define COLS     16
#define ROWS     2
#define CELLS    32
#define MAX_LEN  31

// Turbo yem: yilan bu uzunluga ulasinca tetiklenir
#define TURBO_THRESHOLD 12
// Turbo yem kac tick sonra kaybolur (8000ms / spd ~ 20-65 tick)
#define TURBO_TIMEOUT_MS 8000UL
// Turbo yakalaninca yilan kac birim kisalir
#define TURBO_SHRINK 4
// Turbo icin puan bonusu
#define TURBO_BONUS  5

const int SPD_BASE = 420;
const int SPD_MIN  = 120;
int getSpd(int sc){ int s=SPD_BASE-(sc/3)*10; return s<SPD_MIN?SPD_MIN:s; }

#define EE_SNAKE 2
int  eeGet(){ int v=((int)EEPROM.read(EE_SNAKE)<<8)|EEPROM.read(EE_SNAKE+1); return(v<0||v>9999)?0:v; }
void eePut(int s){ if(s>eeGet()){EEPROM.write(EE_SNAKE,(s>>8)&0xFF);EEPROM.write(EE_SNAKE+1,s&0xFF);} }

// ── Sprites ───────────────────────────────────────────────────────────────────
// Slot 3 (S_TURBO) oyun sirasinda turbo yem olarak kullaniliyor.
// Olum ekraninda loadSprites() ile dead-head sprite'i geri yukleniyor.
byte spHead[8] = {0b00000,0b01110,0b11011,0b01110,0b00100,0b00100,0b01010,0b10001};
byte spBody[8] = {0b00000,0b00000,0b01110,0b11111,0b11111,0b01110,0b00000,0b00000};
byte spFood[8] = {0b00100,0b01110,0b11111,0b11111,0b01110,0b00100,0b00000,0b00000};
byte spDead[8] = {0b00000,0b10001,0b01010,0b00100,0b01010,0b10001,0b00000,0b00000};
// Turbo yem: yildiz/elmas sekli — dikkat cekici olmali
byte spTurbo[8]= {0b00100,0b10101,0b01110,0b11111,0b01110,0b10101,0b00100,0b00000};

#define S_HEAD  0
#define S_BODY  1
#define S_FOOD  2
#define S_DEAD  3   // oyun sirasinda S_TURBO olarak da kullaniliyor
#define S_TURBO 3

void loadSprites(){
  lcd.createChar(S_HEAD,  spHead);
  lcd.createChar(S_BODY,  spBody);
  lcd.createChar(S_FOOD,  spFood);
  lcd.createChar(S_DEAD,  spDead);
}

void loadTurboSprite(){
  // Slot 3'e turbo yem sprite'ini yukle (oyun sirasinda)
  lcd.createChar(S_TURBO, spTurbo);
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

// ── Buttons ───────────────────────────────────────────────────────────────────
bool upHeld=false;  unsigned long upTime=0;
bool dnHeld=false;  unsigned long dnTime=0;
bool leHeld=false;  unsigned long leTime=0;
bool riHeld=false;  unsigned long riTime=0;

bool upPressed(){
  bool d=(digitalRead(PIN_UP)==LOW);
  if(d&&!upHeld&&millis()-upTime>150){upHeld=true;upTime=millis();return true;}
  if(!d) upHeld=false; return false;
}
bool downPressed(){
  bool d=(digitalRead(PIN_DOWN)==LOW);
  if(d&&!dnHeld&&millis()-dnTime>150){dnHeld=true;dnTime=millis();return true;}
  if(!d) dnHeld=false; return false;
}
bool leftPressed(){
  bool d=(digitalRead(PIN_LEFT)==LOW);
  if(d&&!leHeld&&millis()-leTime>150){leHeld=true;leTime=millis();return true;}
  if(!d) leHeld=false; return false;
}
bool rightPressed(){
  bool d=(digitalRead(PIN_RIGHT)==LOW);
  if(d&&!riHeld&&millis()-riTime>150){riHeld=true;riTime=millis();return true;}
  if(!d) riHeld=false; return false;
}
bool anyPressed(){ return upPressed()||downPressed()||leftPressed()||rightPressed(); }

void boop(int f,int d){ tone(PIN_BUZZ,f,d); }

int           flashPin=-1;
unsigned long flashEnd=0;
void flashNB(int pin,int ms){
  if(flashPin>=0&&flashPin!=pin) digitalWrite(flashPin,LOW);
  digitalWrite(pin,HIGH); flashPin=pin; flashEnd=millis()+ms;
}
void flashUpdate(){
  if(flashPin>=0&&millis()>=flashEnd){ digitalWrite(flashPin,LOW); flashPin=-1; }
}

enum AppState { ST_MENU, ST_GAME };
AppState appState = ST_MENU;

#define DIR_RIGHT 0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_UP    3

byte snakeCell[MAX_LEN];
int  snakeLen;

byte dirQueue[2];
int  dirQueueLen;
byte currentDir;

// Normal yem
byte foodCell;

// Turbo yem
bool          turboActive  = false;
byte          turboCell    = 0;
unsigned long turboSpawnMs = 0;   // millis() turbo spawn aninda

int  score, best, spd;
bool dead;
unsigned long lastTick;

// ── Helpers ───────────────────────────────────────────────────────────────────
byte cellOf(int col,int row){ return (byte)(row*COLS+col); }
int  colOf(byte cell)       { return cell%COLS; }
int  rowOf(byte cell)       { return cell/COLS; }

byte stepCell(byte cell, byte dir){
  int c=colOf(cell), r=rowOf(cell);
  if(dir==DIR_RIGHT) c=(c+1)%COLS;
  else if(dir==DIR_LEFT)  c=(c+COLS-1)%COLS;
  else if(dir==DIR_DOWN)  r=(r+1)%ROWS;
  else if(dir==DIR_UP)    r=(r+ROWS-1)%ROWS;
  return cellOf(c,r);
}

bool snakeOccupies(byte cell, bool skipTail){
  int limit = skipTail ? snakeLen-1 : snakeLen;
  for(int i=0;i<limit;i++) if(snakeCell[i]==cell) return true;
  return false;
}

// ── Spawn normal yem ──────────────────────────────────────────────────────────
void spawnFood(){
  int freeCells = CELLS - snakeLen - (turboActive ? 1 : 0);
  if(freeCells<=0) return;
  int target=random(freeCells), freeIdx=0;
  for(int i=0;i<CELLS;i++){
    byte c=(byte)i;
    if(snakeOccupies(c,false)) continue;
    if(turboActive && c==turboCell) continue;
    if(freeIdx==target){ foodCell=c; return; }
    freeIdx++;
  }
}

// ── Spawn turbo yem ───────────────────────────────────────────────────────────
void spawnTurbo(){
  // Slot 3'e turbo sprite yukle
  loadTurboSprite();
  // bInit() ile buffer invalidate et ki sprite degisimi ekrana yansisın
  bInit();

  int freeCells = CELLS - snakeLen - 1; // -1 normal yem icin
  if(freeCells<=0) return;
  int target=random(freeCells), freeIdx=0;
  for(int i=0;i<CELLS;i++){
    byte c=(byte)i;
    if(snakeOccupies(c,false)) continue;
    if(c==foodCell) continue;
    if(freeIdx==target){ turboCell=c; turboActive=true; turboSpawnMs=millis(); return; }
    freeIdx++;
  }
}

// ── Turbo yem kayboldu ────────────────────────────────────────────────────────
void removeTurbo(){
  turboActive=false;
  // Slot 3'u dead-head sprite'a geri yukle
  lcd.createChar(S_DEAD, spDead);
  bInit();  // buffer invalidate — sprite degisimi gorunsun
}

// ── Yilan kisaltma ────────────────────────────────────────────────────────────
void shrinkSnake(int amount){
  snakeLen -= amount;
  if(snakeLen < 3) snakeLen = 3;  // minimum uzunluk 3
}

// ── Direction queue ───────────────────────────────────────────────────────────
void enqueueDir(byte newDir){
  byte lastDir=(dirQueueLen>0)?dirQueue[dirQueueLen-1]:currentDir;
  if((newDir+2)%4==lastDir) return;
  if(dirQueueLen>0&&dirQueue[dirQueueLen-1]==newDir) return;
  if(dirQueueLen<2) dirQueue[dirQueueLen++]=newDir;
}

// ── Reset ─────────────────────────────────────────────────────────────────────
void resetGame(){
  score=0; dead=false; spd=getSpd(0); best=eeGet();
  snakeLen=3; currentDir=DIR_RIGHT; dirQueueLen=0;
  snakeCell[0]=cellOf(9,0);
  snakeCell[1]=cellOf(8,0);
  snakeCell[2]=cellOf(7,0);
  turboActive=false;
  loadSprites();   // slot 3 = dead-head (normal baslangic)
  spawnFood();
  lastTick=millis();
  bInit(); bClr(); bFlush();
}

// ── Input ─────────────────────────────────────────────────────────────────────
void readInput(){
  if(upPressed())    enqueueDir(DIR_UP);
  if(downPressed())  enqueueDir(DIR_DOWN);
  if(leftPressed())  enqueueDir(DIR_LEFT);
  if(rightPressed()) enqueueDir(DIR_RIGHT);
}

// ── Tick ──────────────────────────────────────────────────────────────────────
void doTick(){
  // Direction queue consume
  if(dirQueueLen>0){
    currentDir=dirQueue[0];
    dirQueue[0]=dirQueue[1];
    dirQueueLen--;
  }

  byte newHead=stepCell(snakeCell[0],currentDir);

  // Self collision
  if(snakeOccupies(newHead,true)){
    dead=true;
    eePut(score);
    if(score>best) best=score;
    // Slot 3'u dead-head sprite'a geri yukle
    lcd.createChar(S_DEAD,spDead);
    bInit();
    flashNB(PIN_RED,400);
    boop(200,400);
    return;
  }

  // Turbo yem timeout kontrolu
  if(turboActive && millis()-turboSpawnMs > TURBO_TIMEOUT_MS){
    removeTurbo();
  }

  bool ateNormal = (newHead==foodCell);
  bool ateTurbo  = (turboActive && newHead==turboCell);

  // Yilani hareket ettir
  if(ateNormal || ateTurbo){
    // Yem yenildi: bas ekle (kuyruk duruyor = buyume)
    if(snakeLen<MAX_LEN){
      for(int i=snakeLen;i>0;i--) snakeCell[i]=snakeCell[i-1];
      snakeLen++;
    } else {
      for(int i=snakeLen-1;i>0;i--) snakeCell[i]=snakeCell[i-1];
    }
  } else {
    // Normal hareket: kuyruk dusuyor
    for(int i=snakeLen-1;i>0;i--) snakeCell[i]=snakeCell[i-1];
  }
  snakeCell[0]=newHead;

  // Normal yem efekti
  if(ateNormal){
    score++;
    spd=getSpd(score);
    spawnFood();
    int pitch=800+(score/5)*100;
    if(pitch>2000) pitch=2000;
    boop(pitch,60);
    flashNB(PIN_GREEN,60);
    if(score%5==0){ delay(70); boop(pitch+200,80); flashNB(PIN_YELLOW,80); }

    // Turbo yem tetikle: ilk kez esige ulastiysa veya her 5 puanda tekrar
    if(!turboActive && snakeLen>=TURBO_THRESHOLD){
      spawnTurbo();
    }
  }

  // Turbo yem efekti
  if(ateTurbo){
    turboActive=false;
    score+=TURBO_BONUS;
    spd=getSpd(score);
    shrinkSnake(TURBO_SHRINK);
    spawnFood();   // yem pozisyonunu guncelle (turbo hucre artik bos)

    // Slot 3'u dead-head'e geri yukle, turbo bitti
    lcd.createChar(S_DEAD,spDead);
    bInit();

    // Turbo ses: hizli inen glissando
    boop(2000,40); delay(50);
    boop(1600,40); delay(50);
    boop(1200,40); delay(50);
    boop(800,80);
    flashNB(PIN_YELLOW,200);

    // Her 5 puanda yeni turbo yem tetiklenebilir
  }

  // Turbo yoksa ve esik asilmissa yeni turbo dene
  if(!turboActive && snakeLen>=TURBO_THRESHOLD){
    // sadece yem yenildiginde tetikleniyor (yukaridaki bloklar)
    // ekstra kontrol: turbo hic aktif degilse ve son yemden bu yana
    // tetikleme olmadiysa — bu tick'te degil, bir sonraki yemde tetiklenecek
  }

  flashUpdate();
}

// ── Render ────────────────────────────────────────────────────────────────────
void doRender(){
  bClr();
  bSp(colOf(foodCell),rowOf(foodCell),S_FOOD);
  if(turboActive) bSp(colOf(turboCell),rowOf(turboCell),S_TURBO);
  for(int i=snakeLen-1;i>=1;i--) bSp(colOf(snakeCell[i]),rowOf(snakeCell[i]),S_BODY);
  bSp(colOf(snakeCell[0]),rowOf(snakeCell[0]),dead?S_DEAD:S_HEAD);
  bFlush();
}

// ── Death screen ──────────────────────────────────────────────────────────────
void showDead(){
  doRender();
  unsigned long t=millis(); while(millis()-t<800) delay(5);

  bClr();
  const char ss[]="SCORE:"; for(int i=0;ss[i];i++) bCh(i,0,ss[i]);
  bCh(9, 0,'0'+(score/100)%10);
  bCh(10,0,'0'+(score/ 10)%10);
  bCh(11,0,'0'+ score     %10);
  const char bs[]="BEST :"; for(int i=0;bs[i];i++) bCh(i,1,bs[i]);
  bCh(9, 1,'0'+(best/100)%10);
  bCh(10,1,'0'+(best/ 10)%10);
  bCh(11,1,'0'+ best     %10);
  bFlush();
  t=millis(); while(millis()-t<1500){if(anyPressed())break;delay(10);}

  if(score>0&&score==best){
    bClr();
    const char rec[]="* NEW RECORD! *"; for(int i=0;rec[i];i++) bCh(i,0,rec[i]);
    char buf[4]; sprintf(buf,"%3d",score); for(int i=0;i<3;i++) bCh(7+i,1,buf[i]);
    bFlush();
    for(int i=0;i<4;i++){
      digitalWrite(PIN_GREEN,HIGH); tone(PIN_BUZZ,2000,80);
      t=millis(); while(millis()-t<200){if(anyPressed())break;delay(1);}
      digitalWrite(PIN_GREEN,LOW);
      t=millis(); while(millis()-t<80) {if(anyPressed())break;delay(1);}
    }
    t=millis(); while(millis()-t<500){if(anyPressed())break;delay(10);}
  }

  bClr();
  const char r1[]="ANY to restart"; for(int i=0;r1[i];i++) bCh(i,0,r1[i]);
  const char r2[]="BEST:";         for(int i=0;r2[i];i++) bCh(i,1,r2[i]);
  bCh(7,1,'0'+(best/100)%10);
  bCh(8,1,'0'+(best/ 10)%10);
  bCh(9,1,'0'+ best     %10);
  bFlush();

  upHeld=false;dnHeld=false;leHeld=false;riHeld=false;
  while(!anyPressed()) delay(10);
  appState=ST_MENU;
}

// ── Menu ──────────────────────────────────────────────────────────────────────
void showMenu(){
  loadSprites(); bInit(); bClr();
  const char m1[]="  SNAKE  GAME  "; for(int i=0;m1[i];i++) bCh(i,0,m1[i]);
  const char m2[]=" ANY to start! "; for(int i=0;m2[i];i++) bCh(i,1,m2[i]);
  bFlush();
  upHeld=false;dnHeld=false;leHeld=false;riHeld=false;
  bool bl=true; unsigned long lb=millis();
  while(!anyPressed()){
    if(millis()-lb>500){lb=millis();bl=!bl;bCh(0,1,bl?'>':' ');bFlush();}
    delay(10);
  }
  boop(1500,80);
  resetGame();
  appState=ST_GAME;
}

// ── Setup & Loop ──────────────────────────────────────────────────────────────
void setup(){
  lcd.init(); lcd.backlight(); loadSprites();
  pinMode(PIN_UP,    INPUT_PULLUP);
  pinMode(PIN_DOWN,  INPUT_PULLUP);
  pinMode(PIN_LEFT,  INPUT_PULLUP);
  pinMode(PIN_RIGHT, INPUT_PULLUP);
  pinMode(PIN_BUZZ,  OUTPUT);
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_YELLOW,OUTPUT);
  pinMode(PIN_RED,   OUTPUT);
  randomSeed(analogRead(A0));
  bInit(); lcd.clear(); appState=ST_MENU;
}

void loop(){
  switch(appState){
    case ST_MENU:
      showMenu();
      break;
    case ST_GAME:
      readInput();
      if(dead){ showDead(); break; }
      if(millis()-lastTick>=(unsigned long)spd){
        lastTick=millis();
        doTick();
        doRender();
      }
      flashUpdate();
      break;
  }
}
