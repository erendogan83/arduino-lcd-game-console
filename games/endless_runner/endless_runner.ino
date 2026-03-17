/*
 * Endless Runner v9 — Eren DOGAN — github.com/erendogan83
 * UP(D2)=top row  DOWN(D3)=bot row  FIRE(D5)=toggle  LEFT(D4)=menu
 * PINS: A4,A5=LCD D6=Buzz D7=Green D8=Yellow D9=Red
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
#define ROW_TOP    0
#define ROW_BOT    1
#define PCOL       2
#define PLAY_END   10
#define MIN_GAP    3
#define MAX_LIVES  5

const int SPD_BASE=220, SPD_MIN=80;
int getSpd(int sc){ int s=SPD_BASE-(sc/5)*5; return s<SPD_MIN?SPD_MIN:s; }
int getBurstChance(int sc){ return sc<50?20:sc<100?40:sc<200?60:80; }
int getBSpawnDelay(int sc){ return sc<50?6:sc<100?5:sc<200?4:3; }

#define EE 0
int  eeGet(){ int v=((int)EEPROM.read(EE)<<8)|EEPROM.read(EE+1); return(v<0||v>9999)?0:v; }
void eePut(int s){ if(s>eeGet()){EEPROM.write(EE,(s>>8)&0xFF);EEPROM.write(EE+1,s&0xFF);} }

byte spPlay[8]={0b00100,0b01110,0b00100,0b01110,0b10101,0b00100,0b01010,0b10001};
byte spObs[8] ={0b00000,0b00100,0b01110,0b01110,0b11111,0b11111,0b11111,0b00000};
byte spBoom[8]={0b10101,0b01110,0b11111,0b01110,0b10101,0b01110,0b10001,0b00000};
byte spHrt[8] ={0b00000,0b01010,0b11111,0b11111,0b01110,0b00100,0b00000,0b00000};
#define S_PLAY 0
#define S_OBS  1
#define S_BOOM 2
#define S_HRT  3

void loadSprites(){
  lcd.createChar(S_PLAY,spPlay); lcd.createChar(S_OBS,spObs);
  lcd.createChar(S_BOOM,spBoom); lcd.createChar(S_HRT,spHrt);
}

char cur[2][16],prv[2][16];
void bClr(){ for(int r=0;r<2;r++) for(int c=0;c<16;c++) cur[r][c]=' '; }
void bCh(int c,int r,char ch){ if(c>=0&&c<16&&r>=0&&r<2) cur[r][c]=ch; }
void bSp(int c,int r,byte s) { if(c>=0&&c<16&&r>=0&&r<2) cur[r][c]=(char)(128+s); }
void bFlush(){
  for(int r=0;r<2;r++) for(int c=0;c<16;c++){
    if(cur[r][c]!=prv[r][c]){
      lcd.setCursor(c,r);
      uint8_t v=(uint8_t)cur[r][c];
      if(v>=128) lcd.write(v-128); else lcd.write(v);
      prv[r][c]=cur[r][c];
    }
  }
}
void bInit(){ for(int r=0;r<2;r++) for(int c=0;c<16;c++) prv[r][c]='\0'; }

// ── Buttons: plain variables only, no structs, no reference params ────────────
bool upHeld=false;   unsigned long upTime=0;
bool dnHeld=false;   unsigned long dnTime=0;
bool fiHeld=false;   unsigned long fiTime=0;
bool leHeld=false;   unsigned long leTime=0;

bool upPressed(){
  bool d=(digitalRead(PIN_UP)==LOW);
  if(d&&!upHeld&&millis()-upTime>200){upHeld=true;upTime=millis();return true;}
  if(!d) upHeld=false; return false;
}
bool downPressed(){
  bool d=(digitalRead(PIN_DOWN)==LOW);
  if(d&&!dnHeld&&millis()-dnTime>200){dnHeld=true;dnTime=millis();return true;}
  if(!d) dnHeld=false; return false;
}
bool firePressed(){
  bool d=(digitalRead(PIN_FIRE)==LOW);
  if(d&&!fiHeld&&millis()-fiTime>200){fiHeld=true;fiTime=millis();return true;}
  if(!d) fiHeld=false; return false;
}
bool leftPressed(){
  bool d=(digitalRead(PIN_LEFT)==LOW);
  if(d&&!leHeld&&millis()-leTime>200){leHeld=true;leTime=millis();return true;}
  if(!d) leHeld=false; return false;
}
bool anyConfirm(){ return firePressed()||leftPressed(); }

void boop(int f,int d){ tone(PIN_BUZZ,f,d); }

// ── Non-blocking flash: plain variables ───────────────────────────────────────
int           flashPin=-1;
unsigned long flashEnd=0;
void flashNB(int pin,int ms){
  if(flashPin>=0&&flashPin!=pin) digitalWrite(flashPin,LOW);
  digitalWrite(pin,HIGH); flashPin=pin; flashEnd=millis()+ms;
}
void flashUpdate(){
  if(flashPin>=0&&millis()>=flashEnd){ digitalWrite(flashPin,LOW); flashPin=-1; }
}

enum AppState { ST_MENU, ST_TUTORIAL, ST_GAME };
AppState appState=ST_MENU;

int  pRow;
int  aX,aRow; bool aScored,aFlipped;
int  bX,bRow; bool bActive,bScored,bFlipped; int bSpawnCountdown;
int  hX,hRow; bool hOn; unsigned long hTimer;
int  score,best,lives,spd; bool dead;
unsigned long lastTick;
int  nextLifeScore;

int lastObsRow=ROW_BOT;
int pickOppositeRow(){
  int c=(lastObsRow==ROW_BOT)?ROW_TOP:ROW_BOT; lastObsRow=c; return c;
}

void spawnA(); void spawnB();

bool canSpawnB(){ return aX<0||(14-aX)>=MIN_GAP; }
void spawnB(){
  bX=14; bRow=pickOppositeRow();
  bScored=false; bFlipped=false; bActive=true; bSpawnCountdown=-1;
}
void spawnA(){
  aX=14; aRow=pickOppositeRow(); aScored=false; aFlipped=false;
  bSpawnCountdown=(!bActive&&random(100)<getBurstChance(score))?getBSpawnDelay(score):-1;
}

void showLifeUp(){
  bClr();
  bCh(0,0,'*');bCh(1,0,'*');
  bCh(3,0,'L');bCh(4,0,'I');bCh(5,0,'F');bCh(6,0,'E');
  bCh(8,0,'U');bCh(9,0,'P');bCh(10,0,'!');
  bCh(13,0,'*');bCh(14,0,'*');
  bCh(2,1,'L');bCh(3,1,'I');bCh(4,1,'V');
  bCh(5,1,'E');bCh(6,1,'S');bCh(7,1,':');bCh(9,1,'0'+lives);
  bFlush();
  boop(1400,60);delay(70);boop(1600,60);delay(70);
  boop(1800,60);delay(70);boop(2200,120);
  for(int i=0;i<2;i++){digitalWrite(PIN_GREEN,HIGH);delay(100);digitalWrite(PIN_GREEN,LOW);delay(80);}
  unsigned long t=millis(); while(millis()-t<800){if(anyConfirm())break;delay(10);}
}

void resetGame(){
  pRow=ROW_BOT; score=0; lives=3; dead=false;
  spd=getSpd(0); best=eeGet(); nextLifeScore=100;
  bSpawnCountdown=-1; lastObsRow=ROW_BOT;
  aX=14; aRow=pickOppositeRow(); aScored=false; aFlipped=false; bActive=false;
  hOn=false; hTimer=millis(); lastTick=millis();
  bInit(); bClr(); bFlush();
}

void renderLives(){
  if(lives<=3){
    bCh(12,0,lives>=1?(char)(128+S_HRT):'-');
    bCh(13,0,lives>=2?(char)(128+S_HRT):'-');
    bCh(14,0,lives>=3?(char)(128+S_HRT):'-');
  } else {
    bCh(12,0,'0'+lives); bCh(13,0,'x'); bCh(14,0,(char)(128+S_HRT));
  }
}

void doTick(){
  // 1. Heart collection before obstacle collision
  if(hOn&&hX==PCOL&&hRow==pRow){
    hOn=false;hTimer=millis();score+=10;spd=getSpd(score);
    boop(2000,50);delay(60);boop(2200,100);flashNB(PIN_YELLOW,120);
  }
  // 2. Obstacle collision before movement
  bool hit=(aX==PCOL&&aRow==pRow)||(bActive&&bX==PCOL&&bRow==pRow);
  if(hit){
    lives--;flashNB(PIN_RED,200);boop(300,200);
    lastObsRow=ROW_BOT;bSpawnCountdown=-1;
    aX=14;aRow=pickOppositeRow();aScored=false;aFlipped=false;bActive=false;
    if(lives<=0){dead=true;eePut(score);if(score>best)best=score;}
    return;
  }
  // 3. Bonus life
  if(score>=nextLifeScore){
    if(lives<MAX_LIVES){lives++;showLifeUp();lastTick=millis();}
    nextLifeScore+=100;
  }
  // 4. Deferred B countdown
  if(bSpawnCountdown>0){ bSpawnCountdown--; }
  else if(bSpawnCountdown==0&&!bActive){
    if(canSpawnB()) spawnB(); else bSpawnCountdown=-1;
  }
  // 5. Move A
  aX--;
  if(score>=100&&!aFlipped&&aX>=5&&random(4)==0){aRow=1-aRow;aFlipped=true;boop(450,20);}
  if(!aScored&&aX==PCOL-1){
    score++;aScored=true;spd=getSpd(score);
    if(score%10==0){boop(1800,50);delay(60);boop(2000,80);flashNB(PIN_GREEN,80);}
    else{boop(1600,40);flashNB(PIN_GREEN,40);}
  }
  if(aX<0) spawnA();
  // 6. Move B
  if(bActive){
    bX--;
    if(score>=100&&!bFlipped&&bX>=5&&random(4)==0){bRow=1-bRow;bFlipped=true;boop(450,20);}
    if(!bScored&&bX==PCOL-1){
      score++;bScored=true;spd=getSpd(score);
      if(score%10==0){boop(1800,50);delay(60);boop(2000,80);flashNB(PIN_GREEN,80);}
      else{boop(1600,40);flashNB(PIN_GREEN,40);}
    }
    if(bX<0) bActive=false;
  }
  // 7. Safety gap
  if(bActive&&aX>=0&&bX>=0&&abs(aX-bX)<MIN_GAP){bActive=false;bSpawnCountdown=-1;}
  // 8. Bonus heart
  if(!hOn&&millis()-hTimer>8000UL){
    hOn=true;hX=14;
    int fr=(bActive&&bX>=0)?bRow:(aX>=0?aRow:-1);
    hRow=(fr==ROW_TOP)?ROW_BOT:(fr==ROW_BOT)?ROW_TOP:(int)random(2);
    hTimer=millis();
  }
  if(hOn){hX--;if(hX<0){hOn=false;hTimer=millis();}}
  flashUpdate();
}

void doRender(){
  bClr();
  bSp(PCOL,pRow,S_PLAY);
  if(aX>=0&&aX<=PLAY_END)          bSp(aX,aRow,S_OBS);
  if(bActive&&bX>=0&&bX<=PLAY_END) bSp(bX,bRow,S_OBS);
  if(hOn&&hX>=0&&hX<=PLAY_END)     bSp(hX,hRow,S_HRT);
  renderLives();
  bCh(12,1,'0'+(score/1000)%10);
  bCh(13,1,'0'+(score/ 100)%10);
  bCh(14,1,'0'+(score/  10)%10);
  bCh(15,1,'0'+ score      %10);
  bFlush();
}

void showGameOver(){
  bInit();bClr();bSp(PCOL,pRow,S_BOOM);bFlush();
  digitalWrite(PIN_RED,HIGH);
  int ff[]={800,600,400,300},fd[]={120,120,120,400};
  for(int i=0;i<4;i++){
    tone(PIN_BUZZ,ff[i],fd[i]);
    unsigned long t=millis();
    while(millis()-t<(unsigned long)fd[i]+30){if(anyConfirm())break;delay(1);}
  }
  noTone(PIN_BUZZ);
  unsigned long w=millis();while(millis()-w<300){if(anyConfirm())break;delay(1);}
  digitalWrite(PIN_RED,LOW);
  bClr();
  const char ss[]="SCORE:";for(int i=0;ss[i];i++)bCh(i,0,ss[i]);
  bCh(9,0,'0'+(score/1000)%10);bCh(10,0,'0'+(score/100)%10);
  bCh(11,0,'0'+(score/10)%10);bCh(12,0,'0'+score%10);
  const char bs[]="BEST :";for(int i=0;bs[i];i++)bCh(i,1,bs[i]);
  bCh(9,1,'0'+(best/1000)%10);bCh(10,1,'0'+(best/100)%10);
  bCh(11,1,'0'+(best/10)%10);bCh(12,1,'0'+best%10);
  bFlush();
  w=millis();while(millis()-w<1500){if(anyConfirm())break;delay(10);}
  if(score>0&&score==best){
    bClr();
    const char rec[]="* NEW RECORD! *";for(int i=0;rec[i];i++)bCh(i,0,rec[i]);
    char buf[5];sprintf(buf,"%4d",score);for(int i=0;i<4;i++)bCh(6+i,1,buf[i]);
    bFlush();
    for(int i=0;i<4;i++){
      digitalWrite(PIN_GREEN,HIGH);tone(PIN_BUZZ,2000,80);
      w=millis();while(millis()-w<200){if(anyConfirm())break;delay(1);}
      digitalWrite(PIN_GREEN,LOW);
      w=millis();while(millis()-w<80){if(anyConfirm())break;delay(1);}
    }
    w=millis();while(millis()-w<600){if(anyConfirm())break;delay(10);}
  }
  bClr();
  const char r1[]="FIRE to restart";for(int i=0;r1[i];i++)bCh(i,0,r1[i]);
  const char r2[]="BEST:";for(int i=0;r2[i];i++)bCh(i,1,r2[i]);
  bCh(8,1,'0'+(best/1000)%10);bCh(9,1,'0'+(best/100)%10);
  bCh(10,1,'0'+(best/10)%10);bCh(11,1,'0'+best%10);
  bFlush();
  fiHeld=false;leHeld=false;
  while(!anyConfirm()) delay(10);
  appState=ST_MENU;
}

void showTutorial(){
  loadSprites();bInit();bClr();
  bCh(0,0,'^');bSp(2,0,S_PLAY);
  bCh(4,0,'U');bCh(5,0,'P');bCh(7,0,'R');bCh(8,0,'O');bCh(9,0,'W');
  bCh(0,1,'v');bSp(2,1,S_OBS);
  bCh(4,1,'E');bCh(5,1,'S');bCh(6,1,'C');bCh(7,1,'A');bCh(8,1,'P');bCh(9,1,'E');
  bFlush();
  unsigned long w=millis();while(millis()-w<2500){if(anyConfirm())break;delay(10);}
  bClr();bSp(2,0,S_HRT);
  bCh(4,0,'C');bCh(5,0,'A');bCh(6,0,'T');bCh(7,0,'C');bCh(8,0,'H');
  bCh(10,0,'+');bCh(11,0,'1');bCh(12,0,'0');
  bFlush();
  w=millis();while(millis()-w<2000){if(anyConfirm())break;delay(10);}
  for(int i=3;i>=1;i--){
    bClr();bCh(7,0,'0'+i);bFlush();boop(1200,80);
    w=millis();
    while(millis()-w<900){
      if(anyConfirm()){
        bClr();bCh(5,0,'G');bCh(6,0,'O');bCh(7,0,'!');bFlush();
        boop(1600,60);delay(60);boop(2000,120);delay(600);
        resetGame();appState=ST_GAME;return;
      }
      delay(10);
    }
  }
  bClr();bCh(5,0,'G');bCh(6,0,'O');bCh(7,0,'!');bFlush();
  boop(1600,60);delay(60);boop(2000,120);delay(600);
  resetGame();appState=ST_GAME;
}

void showMenu(){
  loadSprites();bInit();bClr();
  const char m1[]="ENDLESS  RUNNER";for(int i=0;m1[i];i++)bCh(i,0,m1[i]);
  const char m2[]=" FIRE to start!";for(int i=0;m2[i];i++)bCh(i,1,m2[i]);
  bFlush();
  fiHeld=false;leHeld=false;
  bool bl=true;unsigned long lb=millis();
  while(!anyConfirm()){
    if(millis()-lb>500){lb=millis();bl=!bl;bCh(0,1,bl?'>':' ');bFlush();}
    delay(10);
  }
  boop(1500,80);appState=ST_TUTORIAL;
}

void setup(){
  lcd.init();lcd.backlight();loadSprites();
  pinMode(PIN_UP,INPUT_PULLUP);   pinMode(PIN_DOWN,INPUT_PULLUP);
  pinMode(PIN_LEFT,INPUT_PULLUP); pinMode(PIN_FIRE,INPUT_PULLUP);
  pinMode(PIN_BUZZ,OUTPUT);       pinMode(PIN_GREEN,OUTPUT);
  pinMode(PIN_YELLOW,OUTPUT);     pinMode(PIN_RED,OUTPUT);
  randomSeed(analogRead(A0));
  bInit();lcd.clear();appState=ST_MENU;
}

void loop(){
  switch(appState){
    case ST_MENU:     showMenu();     break;
    case ST_TUTORIAL: showTutorial(); break;
    case ST_GAME:
      if(dead){showGameOver();break;}
      if(upPressed()  &&pRow!=ROW_TOP){pRow=ROW_TOP;boop(1200,35);}
      if(downPressed()&&pRow!=ROW_BOT){pRow=ROW_BOT;boop(1000,35);}
      if(firePressed())               {pRow=1-pRow; boop(1100,35);}
      if(millis()-lastTick>=(unsigned long)spd){
        lastTick=millis();doTick();doRender();
      }
      flashUpdate();
      break;
  }
}
