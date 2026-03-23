/*
 * Arduino LCD Game Console
 * Author  : Eren DOGAN
 * GitHub  : https://github.com/erendogan83/arduino-lcd-game-console
 *
 * 5 games on a 16x2 I2C LCD with Arduino Nano.
 *
 * HARDWARE:
 *   A4/A5 = LCD I2C
 *   D2=UP  D3=DOWN  D4=LEFT  D5=FIRE
 *   D6=Buzzer  D7=Green  D8=Yellow  D9=Red
 *
 * EEPROM MAP:
 *   0-1  : Endless Runner
 *   2-3  : Snake
 *   4-7  : reserved
 *   8-9  : Reaction Game
 *   10-11: Whack-a-Mole
 *   12-13: Memory Game
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27,16,2);

#define PIN_UP     2
#define PIN_DOWN   3
#define PIN_LEFT   4
#define PIN_FIRE   5
#define PIN_BUZZ   6
#define PIN_GREEN  7
#define PIN_YELLOW 8
#define PIN_RED    9

// ═══════════════════════════════════════════════════════════════
// ENGINE — DOUBLE BUFFER
// ═══════════════════════════════════════════════════════════════
#define COLS 16
#define ROWS 2
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

// ═══════════════════════════════════════════════════════════════
// ENGINE — BUTTONS
// ═══════════════════════════════════════════════════════════════
#define BTN_LOCK 250
bool upPrev=false;  unsigned long upLock=0;
bool dnPrev=false;  unsigned long dnLock=0;
bool lePrev=false;  unsigned long leLock=0;
bool fiPrev=false;  unsigned long fiLock=0;

void btnReset(){ upPrev=false;dnPrev=false;lePrev=false;fiPrev=false; }

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
int readAnyButton(){
  if(leftPressed()) return 0;
  if(upPressed())   return 1;
  if(downPressed()) return 2;
  if(firePressed()) return 3;
  return -1;
}

// ═══════════════════════════════════════════════════════════════
// ENGINE — LED FLASH
// ═══════════════════════════════════════════════════════════════
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
  digitalWrite(PIN_GREEN,LOW);digitalWrite(PIN_YELLOW,LOW);digitalWrite(PIN_RED,LOW);
  flashPin=-1;
}

// ═══════════════════════════════════════════════════════════════
// ENGINE — EEPROM
// ═══════════════════════════════════════════════════════════════
int  eeRead(int addr){ int v=((int)EEPROM.read(addr)<<8)|EEPROM.read(addr+1); return(v<0||v>9999)?0:v; }
void eeWrite(int addr,int s){ if(s>eeRead(addr)){EEPROM.write(addr,(s>>8)&0xFF);EEPROM.write(addr+1,s&0xFF);} }

// ═══════════════════════════════════════════════════════════════
// ENGINE — SHARED ANIMATIONS
// ═══════════════════════════════════════════════════════════════
void engineCountdown(){
  unsigned long t;
  for(int i=3;i>=1;i--){
    bClr(); bCh(7,0,'0'+i);
    bCh(5,1,'R');bCh(6,1,'E');bCh(7,1,'A');
    bCh(8,1,'D');bCh(9,1,'Y');bCh(10,1,'!');
    bFlush();
    tone(PIN_BUZZ,i==1?1200:1000,80);
    t=millis(); while(millis()-t<900){if(anyPressed())break;delay(10);}
  }
  bClr(); bCh(5,0,'G');bCh(6,0,'O');bCh(7,0,'!'); bFlush();
  tone(PIN_BUZZ,1600,60); delay(70); tone(PIN_BUZZ,2000,120); delay(400);
}

void engineGameOver(int score, int best, int eeAddr){
  ledsOff();
  eeWrite(eeAddr, score);
  if(score>best) best=score;
  bClr();
  const char g1[]="  GAME  OVER   "; for(int i=0;g1[i];i++) bCh(i,0,g1[i]);
  bCh(0,1,'S');bCh(1,1,'C');bCh(2,1,'R');bCh(3,1,':');
  bCh(4,1,'0'+(score/1000)%10);bCh(5,1,'0'+(score/100)%10);
  bCh(6,1,'0'+(score/10)%10); bCh(7,1,'0'+score%10);
  bCh(9,1,'B');bCh(10,1,'S');bCh(11,1,'T');bCh(12,1,':');
  bCh(13,1,'0'+(best/100)%10);bCh(14,1,'0'+(best/10)%10);bCh(15,1,'0'+best%10);
  bFlush();
  unsigned long t=millis(); while(millis()-t<2000){if(anyPressed())break;delay(10);}

  if(score>0&&score==best){
    bClr();
    const char rec[]="* NEW RECORD! *"; for(int i=0;rec[i];i++) bCh(i,0,rec[i]);
    bCh(6,1,'0'+(score/10)%10);bCh(7,1,'0'+score%10); bFlush();
    for(int i=0;i<4;i++){
      digitalWrite(PIN_GREEN,HIGH); tone(PIN_BUZZ,2000,80);
      t=millis(); while(millis()-t<200){if(anyPressed())break;delay(5);}
      digitalWrite(PIN_GREEN,LOW);
      t=millis(); while(millis()-t<100){if(anyPressed())break;delay(5);}
    }
    t=millis(); while(millis()-t<400){if(anyPressed())break;delay(10);}
  }
  bClr();
  const char r1[]="FIRE to restart"; for(int i=0;r1[i];i++) bCh(i,0,r1[i]);
  const char r2[]="BEST:";          for(int i=0;r2[i];i++) bCh(i,1,r2[i]);
  bCh(6,1,'0'+(best/100)%10);bCh(7,1,'0'+(best/10)%10);bCh(8,1,'0'+best%10);
  bFlush();
  btnReset(); while(!firePressed()) delay(10);
}

// ═══════════════════════════════════════════════════════════════
// GAME 1 — ENDLESS RUNNER
// ═══════════════════════════════════════════════════════════════
byte erSpPlay[8]={0b00100,0b01110,0b00100,0b01110,0b10101,0b00100,0b01010,0b10001};
byte erSpObs[8] ={0b00000,0b00100,0b01110,0b01110,0b11111,0b11111,0b11111,0b00000};
byte erSpBoom[8]={0b10101,0b01110,0b11111,0b01110,0b10101,0b00000,0b00000,0b00000};
byte erSpHrt[8] ={0b00000,0b01010,0b11111,0b11111,0b01110,0b00100,0b00000,0b00000};

int erGetSpd(int sc){ int s=220-(sc/5)*5; return s<80?80:s; }

// Endless Runner state (global to allow helper functions)
static int  er_pRow,er_aX,er_aRow,er_bX,er_bRow,er_hX,er_hRow;
static int  er_score,er_lives,er_spd;
static bool er_aScored,er_aFlipped,er_bActive,er_bScored,er_bFlipped;
static bool er_hOn,er_lastWasBot;
static int  er_consCount,er_lastPat;

int erPickRow(){
  int row;
  if(random(100)<30){
    if(er_lastPat==0){row=0;er_lastPat=1;}
    else if(er_lastPat==1){row=1;er_lastPat=0;}
    else{row=random(2);er_lastPat=(row==1)?0:1;}
    er_consCount++;
    if(er_consCount>3){er_consCount=0;er_lastPat=-1;row=random(2);}
  } else {
    row=er_lastWasBot?0:1;
    if(random(10)>=7) row=random(2);
    er_consCount=0; er_lastPat=-1;
  }
  return row;
}
void erSpawnB(){
  er_bX=14; er_bRow=erPickRow(); er_lastWasBot=(er_bRow==1);
  er_bScored=false; er_bFlipped=false; er_bActive=true;
}
void erSpawnA(){
  er_aX=14; er_aRow=erPickRow(); er_lastWasBot=(er_aRow==1);
  er_aScored=false; er_aFlipped=false;
  if(!er_bActive&&random(100)<60&&(14-er_aX)>=2) erSpawnB();
}

void runEndlessRunner(){
  lcd.createChar(0,erSpPlay); lcd.createChar(1,erSpObs);
  lcd.createChar(2,erSpBoom); lcd.createChar(3,erSpHrt);
  bInit(); bClr();

  bSp(4,0,3); bCh(6,0,'C');bCh(7,0,'A');bCh(8,0,'T');bCh(9,0,'C');bCh(10,0,'H');
  bSp(4,1,1); bCh(6,1,'E');bCh(7,1,'S');bCh(8,1,'C');bCh(9,1,'A');bCh(10,1,'P');bCh(11,1,'E');
  bFlush();
  unsigned long t=millis(); while(millis()-t<2000){if(firePressed())break;delay(10);}
  engineCountdown();

  er_pRow=1; er_aX=14; er_aRow=0; er_bActive=false;
  er_hOn=false; er_score=0; er_lives=3; er_spd=erGetSpd(0);
  er_aScored=false; er_aFlipped=false; er_bScored=false; er_bFlipped=false;
  er_lastWasBot=false; er_consCount=0; er_lastPat=-1;
  int er_best=eeRead(0);
  unsigned long lastTick=millis(), hTimer=millis();
  bool dead=false;
  btnReset();  // drain any button state left over from tutorial/countdown

  while(true){
    if(firePressed()){ er_pRow=1-er_pRow; tone(PIN_BUZZ,1200,35); }
    if(leftPressed()) return;

    if(millis()-lastTick>=(unsigned long)er_spd){
      lastTick=millis();
      er_aX--;
      if(er_score>=100&&!er_aFlipped&&er_aX>=5&&random(4)==0){er_aRow=1-er_aRow;er_aFlipped=true;tone(PIN_BUZZ,450,20);}
      if(er_aX<0) erSpawnA();

      if(er_bActive){
        er_bX--;
        if(er_score>=100&&!er_bFlipped&&er_bX>=5&&random(4)==0){er_bRow=1-er_bRow;er_bFlipped=true;tone(PIN_BUZZ,450,20);}
        if(er_bX<0) er_bActive=false;
      }

      if(!er_hOn&&millis()-hTimer>8000UL){er_hOn=true;er_hX=14;er_hRow=random(2);hTimer=millis();}
      if(er_hOn){
        er_hX--;
        if(er_hX<0){er_hOn=false;hTimer=millis();}
        else if(er_hX==2&&er_hRow==er_pRow){
          er_hOn=false;hTimer=millis();er_score+=10;er_spd=erGetSpd(er_score);
          tone(PIN_BUZZ,2000,50);delay(60);tone(PIN_BUZZ,2200,100);flashNB(PIN_YELLOW,100);
        }
      }

      // Collision check at player column (PCOL=2)
      bool hitA=(er_aX==2&&er_aRow==er_pRow);
      bool hitB=(er_bActive&&er_bX==2&&er_bRow==er_pRow);
      if(hitA||hitB){
        er_lives--;flashNB(PIN_RED,150);tone(PIN_BUZZ,300,200);
        er_aX=14;er_aRow=erPickRow();er_lastWasBot=(er_aRow==1);
        er_aScored=false;er_aFlipped=false;er_bActive=false;
        if(er_lives<=0) dead=true;
      } else {
        // Score: obstacle passed player column safely
        if(!er_aScored&&er_aX==1){
          er_score++;er_aScored=true;er_spd=erGetSpd(er_score);
          if(er_score%10==0){tone(PIN_BUZZ,1800,50);delay(60);tone(PIN_BUZZ,2000,80);flashNB(PIN_GREEN,80);}
          else{tone(PIN_BUZZ,1600,40);flashNB(PIN_GREEN,40);}
        }
        if(er_bActive&&!er_bScored&&er_bX==1){
          er_score++;er_bScored=true;er_spd=erGetSpd(er_score);
          if(er_score%10==0){tone(PIN_BUZZ,1800,50);delay(60);tone(PIN_BUZZ,2000,80);flashNB(PIN_GREEN,80);}
          else{tone(PIN_BUZZ,1600,40);flashNB(PIN_GREEN,40);}
        }
      }
      if(er_bActive&&er_aX>=0&&er_bX>=0&&abs(er_aX-er_bX)<2){er_bActive=false;tone(PIN_BUZZ,600,30);}

      bClr();
      bSp(2,er_pRow,0);
      if(er_aX>=0&&er_aX<=10) bSp(er_aX,er_aRow,1);
      if(er_bActive&&er_bX>=0&&er_bX<=10) bSp(er_bX,er_bRow,1);
      if(er_hOn&&er_hX>=0&&er_hX<=10) bSp(er_hX,er_hRow,3);
      bCh(12,0,er_lives>=1?(char)(128+3):'-');
      bCh(13,0,er_lives>=2?(char)(128+3):'-');
      bCh(14,0,er_lives>=3?(char)(128+3):'-');
      bCh(12,1,'0'+(er_score/1000)%10);bCh(13,1,'0'+(er_score/100)%10);
      bCh(14,1,'0'+(er_score/10)%10); bCh(15,1,'0'+er_score%10);
      bFlush(); flashUpdate();
    }

    if(dead){
      bClr(); bSp(2,er_pRow,2); bFlush();
      digitalWrite(PIN_RED,HIGH);
      int ff[]={800,600,400,300}; int fd[]={120,120,120,400};
      for(int i=0;i<4;i++){tone(PIN_BUZZ,ff[i],fd[i]);delay(fd[i]+30);}
      noTone(PIN_BUZZ); delay(200); digitalWrite(PIN_RED,LOW);
      engineGameOver(er_score,er_best,0);
      return;
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// GAME 2 — SNAKE
// ═══════════════════════════════════════════════════════════════
#define SN_MAX 32
byte snSpHead[8]={0b00000,0b01110,0b11011,0b11111,0b11111,0b01110,0b00000,0b00000};
byte snSpBody[8]={0b00000,0b01110,0b01110,0b01110,0b01110,0b01110,0b00000,0b00000};
byte snSpFood[8]={0b00100,0b01110,0b11111,0b11111,0b01110,0b00100,0b00000,0b00000};
byte snSpTurbo[8]={0b10101,0b01110,0b11111,0b01110,0b10101,0b00000,0b00000,0b00000};

int snGetSpd(int sc){ int s=420-(sc/3)*20; return s<120?120:s; }

static int  sn_bodyX[SN_MAX], sn_bodyY[SN_MAX];
static int  sn_length, sn_dir, sn_score, sn_foodX, sn_foodY;
static bool sn_turboActive;
static unsigned long sn_turboTimer;

void snPlaceFood(bool turbo){
  bool ok=false;
  while(!ok){
    int fx=random(16), fy=random(2); ok=true;
    for(int i=0;i<sn_length;i++) if(sn_bodyX[i]==fx&&sn_bodyY[i]==fy){ok=false;break;}
    if(ok){sn_foodX=fx;sn_foodY=fy;}
  }
  sn_turboActive=turbo;
  if(turbo) sn_turboTimer=millis()+8000UL;
}

void runSnake(){
  lcd.createChar(0,snSpHead); lcd.createChar(1,snSpBody);
  lcd.createChar(2,snSpFood); lcd.createChar(3,snSpTurbo);
  bInit(); bClr();

  bCh(0,0,'U');bCh(1,0,'/');bCh(2,0,'D');bCh(3,0,'/');bCh(4,0,'L');bCh(5,0,'/');
  bCh(6,0,'F');bCh(7,0,'I');bCh(8,0,'R');bCh(9,0,'E');
  bCh(0,1,'E');bCh(1,1,'A');bCh(2,1,'T');bCh(4,1,'&');bCh(6,1,'G');
  bCh(7,1,'R');bCh(8,1,'O');bCh(9,1,'W');bCh(10,1,'!');
  bFlush();
  unsigned long t=millis(); while(millis()-t<2000){if(anyPressed())break;delay(10);}
  engineCountdown();

  sn_length=3; sn_dir=1; sn_score=0;
  int sn_best=eeRead(2);
  sn_turboActive=false;
  for(int i=0;i<sn_length;i++){sn_bodyX[i]=7-i;sn_bodyY[i]=0;}
  snPlaceFood(false);
  int spd=snGetSpd(0);
  unsigned long lastTick=millis();
  bool gameOver=false;

  while(!gameOver){
    if(upPressed()   &&sn_dir!=2) sn_dir=0;
    if(firePressed() &&sn_dir!=3) sn_dir=1;
    if(downPressed() &&sn_dir!=0) sn_dir=2;
    if(leftPressed() &&sn_dir!=1) sn_dir=3;

    if(sn_turboActive&&millis()>sn_turboTimer){sn_turboActive=false;snPlaceFood(false);}

    if(millis()-lastTick>=(unsigned long)spd){
      lastTick=millis();
      int nx=sn_bodyX[0], ny=sn_bodyY[0];
      if(sn_dir==0) ny=(ny-1+2)%2;
      else if(sn_dir==1) nx=(nx+1)%16;
      else if(sn_dir==2) ny=(ny+1)%2;
      else nx=(nx-1+16)%16;

      for(int i=0;i<sn_length-1;i++){
        if(sn_bodyX[i]==nx&&sn_bodyY[i]==ny){gameOver=true;break;}
      }
      if(gameOver) break;

      for(int i=sn_length-1;i>0;i--){sn_bodyX[i]=sn_bodyX[i-1];sn_bodyY[i]=sn_bodyY[i-1];}
      sn_bodyX[0]=nx; sn_bodyY[0]=ny;

      if(nx==sn_foodX&&ny==sn_foodY){
        if(sn_turboActive){
          sn_score+=5;
          int shrink=4; if(sn_length-shrink<3) shrink=sn_length-3;
          sn_length-=shrink; sn_turboActive=false;
          tone(PIN_BUZZ,2000,60);delay(70);tone(PIN_BUZZ,2400,120);flashNB(PIN_YELLOW,200);
          snPlaceFood(false);
        } else {
          sn_score++;
          if(sn_length<SN_MAX) sn_length++;
          spd=snGetSpd(sn_score);
          tone(PIN_BUZZ,1600,40); flashNB(PIN_GREEN,40);
          snPlaceFood(sn_length>=12&&random(100)<40);
        }
      }

      bClr();
      bSp(sn_foodX,sn_foodY,sn_turboActive?3:2);
      for(int i=sn_length-1;i>=0;i--) bSp(sn_bodyX[i],sn_bodyY[i],i==0?0:1);
      bFlush(); flashUpdate();
    }
  }
  tone(PIN_BUZZ,300,400);flashNB(PIN_RED,400);delay(400);ledsOff();
  engineGameOver(sn_score,sn_best,2);
}

// ═══════════════════════════════════════════════════════════════
// GAME 3 — REACTION GAME
// ═══════════════════════════════════════════════════════════════
byte rgSpTarget[8]={0b00100,0b01110,0b11111,0b11111,0b11111,0b01110,0b00100,0b00000};
byte rgSpMarker[8]={0b00000,0b00100,0b01110,0b11111,0b01110,0b00100,0b00000,0b00000};
byte rgSpHit[8]   ={0b10101,0b01110,0b11111,0b11111,0b11111,0b01110,0b10101,0b00000};
byte rgSpMiss[8]  ={0b10001,0b11011,0b01110,0b00100,0b01110,0b11011,0b10001,0b00000};

int rgGetSpd(int sc){ int s=300-(sc/5)*8; return s<80?80:s; }
int rgGetBlk(int sc){ int s=700-(sc/5)*20; return s<200?200:s; }

static int  rg_tCol[2];
static bool rg_tActive[2], rg_tBlink[2];
static int  rg_markerCol;

void rgSpawnTgt(int slot){
  int valid[COLS]; int vn=0;
  for(int c=2;c<=13;c++){
    if(c==rg_markerCol) continue;
    if(rg_tActive[1-slot]&&c==rg_tCol[1-slot]) continue;
    valid[vn++]=c;
  }
  rg_tCol[slot]=(vn>0)?valid[random(vn)]:2+random(12);
  rg_tActive[slot]=true; rg_tBlink[slot]=true;
}

void runReactionGame(){
  lcd.createChar(0,rgSpTarget); lcd.createChar(1,rgSpMarker);
  lcd.createChar(2,rgSpHit);   lcd.createChar(3,rgSpMiss);
  bInit(); bClr();

  bCh(0,0,'M');bCh(1,0,'A');bCh(2,0,'R');bCh(3,0,'K');bCh(4,0,'E');bCh(5,0,'R');
  bCh(7,0,'M');bCh(8,0,'O');bCh(9,0,'V');bCh(10,0,'E');bCh(11,0,'S');
  bSp(0,1,1);
  bCh(2,1,'F');bCh(3,1,'I');bCh(4,1,'R');bCh(5,1,'E');
  bCh(7,1,'O');bCh(8,1,'N');bCh(10,1,'T');bCh(11,1,'G');bCh(12,1,'T');
  bFlush();
  unsigned long t=millis(); while(millis()-t<2500){if(anyPressed())break;delay(10);}
  engineCountdown();

  rg_markerCol=2;
  int markerDir=1, score=0, best=eeRead(8), lives=3, combo=0, multiplier=1;
  bool gameOver=false;
  bool firePrevRG=false; unsigned long fireLockRG=0;
  bool showFlash=false; unsigned long flashMsgEnd=0;
  bool flashIsHit=false; int flashCol=0;
  unsigned long lastMarkerTick=millis(), lastBlinkTick=millis();
  int markerSpd=rgGetSpd(0), blinkSpd=rgGetBlk(0);
  rg_tActive[0]=false; rg_tActive[1]=false;
  rgSpawnTgt(0);

  while(!gameOver){
    unsigned long now=millis();

    if(now-lastMarkerTick>=(unsigned long)markerSpd){
      lastMarkerTick=now;
      rg_markerCol+=markerDir;
      if(rg_markerCol>=COLS){rg_markerCol=COLS-1;markerDir=-1;}
      if(rg_markerCol<0){rg_markerCol=0;markerDir=1;}
    }
    if(now-lastBlinkTick>=(unsigned long)blinkSpd){
      lastBlinkTick=now;
      for(int s=0;s<2;s++) if(rg_tActive[s]) rg_tBlink[s]=!rg_tBlink[s];
    }

    bool fireNow=(digitalRead(PIN_FIRE)==LOW);
    bool fireEdge=(fireNow&&!firePrevRG); firePrevRG=fireNow;
    if(fireEdge&&now>=fireLockRG){
      fireLockRG=now+250;
      int hitSlot=-1;
      for(int s=0;s<2;s++) if(rg_tActive[s]&&rg_markerCol==rg_tCol[s]){hitSlot=s;break;}
      flashCol=rg_markerCol;
      if(hitSlot>=0){
        combo++; multiplier=(combo>=5)?3:(combo>=3)?2:1;
        score+=10*multiplier;
        tone(PIN_BUZZ,1800,100);flashNB(PIN_GREEN,120);
        rg_tActive[hitSlot]=false; rgSpawnTgt(hitSlot);
        if(score>=30&&!rg_tActive[1]) rgSpawnTgt(1);
        markerSpd=rgGetSpd(score); blinkSpd=rgGetBlk(score);
        flashIsHit=true; showFlash=true; flashMsgEnd=now+300;
        lastMarkerTick=now;
      } else {
        combo=0; multiplier=1; lives--;
        tone(PIN_BUZZ,250,300);flashNB(PIN_RED,300);
        lastMarkerTick=now;
        flashIsHit=false; showFlash=true; flashMsgEnd=now+300;
        if(lives<=0) gameOver=true;
      }
    }
    if(leftPressed()) return;

    bClr();
    for(int s=0;s<2;s++) if(rg_tActive[s]&&rg_tBlink[s]) bSp(rg_tCol[s],0,0);
    bCh(14,0,(char)(0x7e)); bCh(15,0,'0'+lives);
    bCh(0,1,'0'+(score/100)%10);bCh(1,1,'0'+(score/10)%10);bCh(2,1,'0'+score%10);
    if(multiplier>1){bCh(4,1,'x');bCh(5,1,'0'+multiplier);}
    bSp(rg_markerCol,1,1);
    if(showFlash&&now<flashMsgEnd) bSp(flashCol,1,flashIsHit?2:3);
    else showFlash=false;
    bFlush(); flashUpdate();
  }
  ledsOff();
  engineGameOver(score,best,8);
}

// ═══════════════════════════════════════════════════════════════
// GAME 4 — WHACK-A-MOLE
// ═══════════════════════════════════════════════════════════════
#define WM_NUM 4
byte wmSpHole[8]={0b00000,0b00000,0b00000,0b00000,0b00000,0b01110,0b11111,0b11111};
byte wmSpMole[8]={0b00000,0b01110,0b11111,0b10101,0b11111,0b01110,0b11111,0b11111};
byte wmSpHit[8] ={0b10101,0b01110,0b11111,0b01110,0b10101,0b01110,0b11111,0b11111};
byte wmSpMiss[8]={0b00000,0b00000,0b10001,0b01110,0b00000,0b01110,0b11111,0b11111};

const int WM_COL[WM_NUM]={ 1, 6, 6, 10 };
const int WM_ROW[WM_NUM]={ 0, 0, 1,  0 };

int wmGetWin(int sc){ int w=2000-(sc/5)*120; return w<650?650:w; }
int wmGetSpn(int sc){ int s=1600-(sc/5)*90;  return s<550?550:s; }

enum WMState { WM_EMPTY, WM_UP, WM_HIT, WM_MISS };
static WMState wm_state[WM_NUM];
static unsigned long wm_timer[WM_NUM];

void wmWhack(int idx){
  if(wm_state[idx]==WM_UP){
    wm_state[idx]=WM_HIT; wm_timer[idx]=millis()+300;
    // score handled in caller
  } else if(wm_state[idx]==WM_EMPTY){
    tone(PIN_BUZZ,350,50);
  }
}

void runWhackAMole(){
  lcd.createChar(0,wmSpHole); lcd.createChar(1,wmSpMole);
  lcd.createChar(2,wmSpHit);  lcd.createChar(3,wmSpMiss);
  bInit(); bClr();

  // Tutorial page 1
  for(int i=0;i<WM_NUM;i++) bSp(WM_COL[i],WM_ROW[i],0);
  bCh(3,0,'<');bCh(5,0,'L');bCh(6,0,'E');bCh(7,0,'F');bCh(8,0,'T');
  bCh(7,1,'^');bCh(9,1,'U');bCh(10,1,'P');
  bFlush();
  unsigned long t=millis(); while(millis()-t<2000){if(anyPressed())break;delay(10);}
  // Tutorial page 2
  bClr();
  for(int i=0;i<WM_NUM;i++) bSp(WM_COL[i],WM_ROW[i],0);
  bCh(7,1,'v');bCh(9,1,'D');bCh(10,1,'N');
  bCh(11,0,'>');bCh(12,0,'F');bCh(13,0,'I');bCh(14,0,'R');bCh(15,0,'E');
  bFlush();
  t=millis(); while(millis()-t<2000){if(anyPressed())break;delay(10);}
  engineCountdown();

  for(int i=0;i<WM_NUM;i++){wm_state[i]=WM_EMPTY;wm_timer[i]=0;}
  int score=0, best=eeRead(10), lives=3;
  bool gameOver=false;
  unsigned long lastSpawn=millis();
  int hitScore=0; // track which whack gave a point

  while(!gameOver){
    // Read buttons once — rising edge consumed here
    bool leBtn=leftPressed(), upBtn=upPressed(), dnBtn=downPressed(), fiBtn=firePressed();
    bool hitOccurred=false;
    if(leBtn){ if(wm_state[0]==WM_UP){wm_state[0]=WM_HIT;wm_timer[0]=millis()+300;hitOccurred=true;} else tone(PIN_BUZZ,350,50); }
    if(upBtn){ if(wm_state[1]==WM_UP){wm_state[1]=WM_HIT;wm_timer[1]=millis()+300;hitOccurred=true;} else tone(PIN_BUZZ,350,50); }
    if(dnBtn){ if(wm_state[2]==WM_UP){wm_state[2]=WM_HIT;wm_timer[2]=millis()+300;hitOccurred=true;} else tone(PIN_BUZZ,350,50); }
    if(fiBtn){ if(wm_state[3]==WM_UP){wm_state[3]=WM_HIT;wm_timer[3]=millis()+300;hitOccurred=true;} else tone(PIN_BUZZ,350,50); }
    if(hitOccurred){score++;tone(PIN_BUZZ,1800,80);flashNB(PIN_GREEN,100);}

    unsigned long now=millis();
    for(int i=0;i<WM_NUM;i++){
      if(wm_state[i]==WM_UP&&now>=wm_timer[i]){
        wm_state[i]=WM_MISS;wm_timer[i]=now+300;
        lives--;tone(PIN_BUZZ,250,200);flashNB(PIN_RED,250);
        if(lives<=0) gameOver=true;
      }
      if((wm_state[i]==WM_HIT||wm_state[i]==WM_MISS)&&now>=wm_timer[i])
        wm_state[i]=WM_EMPTY;
    }
    if(now-lastSpawn>=(unsigned long)wmGetSpn(score)){
      lastSpawn=now;
      int empty[WM_NUM]; int en=0;
      for(int i=0;i<WM_NUM;i++) if(wm_state[i]==WM_EMPTY) empty[en++]=i;
      if(en>0){
        int active=0;
        for(int i=0;i<WM_NUM;i++) if(wm_state[i]==WM_UP) active++;
        int maxM=(score>=15)?2:1;
        if(active<maxM){
          int pick=empty[random(en)];
          wm_state[pick]=WM_UP;wm_timer[pick]=now+wmGetWin(score);
        }
      }
    }

    bClr();
    for(int i=0;i<WM_NUM;i++){
      switch(wm_state[i]){
        case WM_EMPTY: bSp(WM_COL[i],WM_ROW[i],0);break;
        case WM_UP:    bSp(WM_COL[i],WM_ROW[i],1);break;
        case WM_HIT:   bSp(WM_COL[i],WM_ROW[i],2);break;
        case WM_MISS:  bSp(WM_COL[i],WM_ROW[i],3);break;
      }
    }
    bCh(14,0,(char)(0x7e));bCh(15,0,'0'+lives);
    bCh(14,1,'0'+(score/10)%10);bCh(15,1,'0'+score%10);
    bFlush(); flashUpdate();
  }
  ledsOff();
  engineGameOver(score,best,10);
}

// ═══════════════════════════════════════════════════════════════
// GAME 5 — MEMORY GAME
// ═══════════════════════════════════════════════════════════════
byte mgSpL[8]={0b11111,0b10000,0b10000,0b11110,0b10000,0b10000,0b11111,0b00000};
byte mgSpU[8]={0b00100,0b01110,0b11111,0b00100,0b00100,0b00100,0b00100,0b00000};
byte mgSpD[8]={0b00100,0b00100,0b00100,0b00100,0b11111,0b01110,0b00100,0b00000};
byte mgSpR[8]={0b11110,0b10001,0b10001,0b11110,0b10010,0b10001,0b10001,0b00000};

const int MG_COL[4]={ 2, 6, 9, 13 };
const int MG_ROW[4]={ 0, 0, 1,  0 };
const int MG_TONE[4]={ 523, 659, 784, 1047 };

int mgGetShow(int r){ int s=700-(r/3)*60; return s<250?250:s; }
int mgGetGap(int r) { int g=250-(r/3)*20; return g<100?100:g; }

void mgDrawBase(int hi, int round){
  bClr();
  for(int i=0;i<4;i++){
    if(i==hi) bSp(MG_COL[i],MG_ROW[i],(byte)i);
    else bCh(MG_COL[i],MG_ROW[i],'.');
  }
  bCh(14,0,'R');bCh(15,0,'0'+(round%10));
  bFlush();
}

void runMemoryGame(){
  lcd.createChar(0,mgSpL); lcd.createChar(1,mgSpU);
  lcd.createChar(2,mgSpD); lcd.createChar(3,mgSpR);
  bInit(); bClr();

  bSp(MG_COL[0],MG_ROW[0],0);bCh(MG_COL[0]+1,MG_ROW[0],'<');
  bSp(MG_COL[1],MG_ROW[1],1);bCh(MG_COL[1]+1,MG_ROW[1],'^');
  bSp(MG_COL[2],MG_ROW[2],2);bCh(MG_COL[2]+1,MG_ROW[2],'v');
  bSp(MG_COL[3],MG_ROW[3],3);bCh(MG_COL[3]+1,MG_ROW[3],'>');
  bFlush();
  unsigned long t=millis(); while(millis()-t<2200){if(anyPressed())break;delay(10);}
  bClr();
  const char p2[]="WATCH,THEN COPY"; for(int i=0;p2[i];i++) bCh(i,0,p2[i]);
  bFlush();
  t=millis(); while(millis()-t<1800){if(anyPressed())break;delay(10);}
  engineCountdown();

  byte sequence[16];
  int seqLen=0, score=0, best=eeRead(12);
  bool gameOver=false;

  while(!gameOver){
    if(seqLen<16) sequence[seqLen++]=random(4);

    bClr();
    const char w[]="  WATCH...     "; for(int i=0;w[i];i++) bCh(i,0,w[i]);
    bCh(14,0,'R');bCh(15,0,'0'+(seqLen%10)); bFlush();
    t=millis(); while(millis()-t<700) delay(5);

    int showMs=mgGetShow(seqLen), gapMs=mgGetGap(seqLen);
    for(int i=0;i<seqLen;i++){
      mgDrawBase(sequence[i],seqLen);
      tone(PIN_BUZZ,MG_TONE[sequence[i]],showMs-40);
      flashNB(PIN_YELLOW,showMs-40);
      t=millis(); while(millis()-t<(unsigned long)showMs) delay(5);
      mgDrawBase(-1,seqLen);
      digitalWrite(PIN_YELLOW,LOW);
      t=millis(); while(millis()-t<(unsigned long)gapMs) delay(5);
    }

    bClr();
    const char y[]="  YOUR TURN!   "; for(int i=0;y[i];i++) bCh(i,0,y[i]);
    bCh(14,0,'R');bCh(15,0,'0'+(seqLen%10)); bFlush();
    t=millis(); while(millis()-t<600) delay(5);

    bool correct=true;
    for(int i=0;i<seqLen&&correct;i++){
      mgDrawBase(-1,seqLen);
      btnReset();
      unsigned long deadline=millis()+4000UL;
      int pressed=-1;
      while(millis()<deadline){ pressed=readAnyButton(); if(pressed>=0) break; delay(5); }
      if(pressed<0){correct=false;break;}
      mgDrawBase(pressed,seqLen);
      tone(PIN_BUZZ,MG_TONE[pressed],120);
      t=millis(); while(millis()-t<150) delay(5);
      mgDrawBase(-1,seqLen);
      if(pressed!=sequence[i]) correct=false;
    }

    if(correct){
      score++;
      tone(PIN_BUZZ,1600,60);delay(80);tone(PIN_BUZZ,2000,100);flashNB(PIN_GREEN,200);
      bClr();
      const char g[]="  CORRECT! +1  "; for(int i=0;g[i];i++) bCh(i,0,g[i]);
      bCh(14,1,'0'+(score/10)%10);bCh(15,1,'0'+score%10); bFlush();
      t=millis(); while(millis()-t<800) delay(5);
      ledsOff();
    } else {
      bClr();
      const char wr[]="  WRONG!       "; for(int i=0;wr[i];i++) bCh(i,0,wr[i]);
      bFlush();
      tone(PIN_BUZZ,200,500);flashNB(PIN_RED,400);
      t=millis(); while(millis()-t<800) delay(5);
      ledsOff(); gameOver=true;
    }
  }
  engineGameOver(score,best,12);
}

// ═══════════════════════════════════════════════════════════════
// MAIN MENU
// ═══════════════════════════════════════════════════════════════
const char* GAME_NAMES[5]={
  "Endless Runner  ",
  "Snake           ",
  "Reaction Game   ",
  "Whack-a-Mole    ",
  "Memory Game     "
};

void showMainMenu(){
  bInit(); bClr();
  int sel=0;
  btnReset();

  // Splash
  const char s1[]=" LCD GAME  CONSOLE"; // trimmed to 16
  const char sl[]="LCD GAME CONSOLE"; for(int i=0;i<16;i++) bCh(i,0,sl[i]);
  const char s2[]="  by Eren DOGAN "; for(int i=0;i<16;i++) bCh(i,1,s2[i]);
  bFlush();
  tone(PIN_BUZZ,1000,60);delay(80);tone(PIN_BUZZ,1400,60);delay(80);tone(PIN_BUZZ,1800,120);
  unsigned long t=millis(); while(millis()-t<1500){if(anyPressed())break;delay(10);}

  while(true){
    bClr();
    const char title[]="SELECT  A  GAME "; for(int i=0;i<16;i++) bCh(i,0,title[i]);
    bCh(0,1,'>');
    for(int i=0;i<15;i++) bCh(i+1,1,GAME_NAMES[sel][i]);
    bFlush();

    t=millis();
    while(millis()-t<150){
      if(upPressed()){   sel=(sel-1+5)%5; break; }
      if(downPressed()){ sel=(sel+1)%5;   break; }
      if(firePressed()){
        tone(PIN_BUZZ,1500,80);
        bInit(); lcd.clear();
        switch(sel){
          case 0: runEndlessRunner(); break;
          case 1: runSnake();         break;
          case 2: runReactionGame();  break;
          case 3: runWhackAMole();    break;
          case 4: runMemoryGame();    break;
        }
        bInit(); lcd.clear();
        break;
      }
      delay(5);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
// SETUP & LOOP
// ═══════════════════════════════════════════════════════════════
void setup(){
  lcd.init(); lcd.backlight();
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
}

void loop(){
  showMainMenu();
}
