/* Arduino Mega - Dual Output (Serial & BT) at 9600 */
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

/* ================= 설정 ================= */
// 화면이 안 나오면 0x3F로 변경
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define DHTPIN   22
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

// [중요] Mega Serial1 (TX=18, RX=19) <-> HC-06 연결
#define BT Serial1 

/* ================= 핀 정의 ================= */
#define TRIG_PIN 23
#define ECHO_PIN 24
#define GAS_PIN  A0

#define BTN_MOVE    30
#define BTN_SELECT  31

#define LED_RED     25 // HEATER
#define LED_GREEN   26 // COOLER
#define LED_YELLOW  27 // HUMIDIFIER

/* ================= 거리 측정 설정 ================= */
#define MIN_DIST_MM 20   
#define MAX_DIST_MM 2000 
long lastStableDistance = 0; 

/* ================= 변수 ================= */
unsigned long elapsedSeconds = 0;
unsigned long lastRunMillis = 0;
unsigned long lastSendMillis = 0;
unsigned long lastLcdMillis = 0;

unsigned long doorOpenCheckMillis = 0; 
bool isDoorChecking = false;

#define CO2_WARMUP_SEC 3600UL

enum RunState { RUN_END, RUN_RUNNING, RUN_PAUSED };
RunState runState = RUN_END;

enum Screen { HOME, MENU, MODE_RUN, STATUS, DOOR_OPEN_SCREEN };
Screen screen = HOME;

enum Mode { LIQUID, DURE, PM };
Mode currentMode = LIQUID;

const char* modeTags[] = { "#1", "#2", "#3" };
const char* modeFullName[] = { "LIQUID", "DURE", "PM" };

int homeCursor = 0;
int menuCursor = 0;
int statusCursor = 0;
int modeView = 0;

float temp = 0.0;
float hum  = 0.0;
long distanceMM = 0;
int co2Val = 0;

unsigned long co2BlinkMillis = 0;
int co2BlinkStep = 0;
bool doorEventActive = false;
bool doorCanPlay = false;

/* ================= 함수 선언 ================= */
void checkBT();
bool pressed(int pin);
long getStableDistance(); 
void updateLED();
void draw();
void sendDataToServer();
void printRunTimeLCD();

/* ================= Setup ================= */
void setup() {
  // 1. PC 시리얼 모니터 (9600)
  Serial.begin(9600); 
  Serial.println("=== ARDUINO STARTED ==="); 

  // 2. 블루투스 (9600)
  BT.begin(9600);    
  Serial.println("Bluetooth Initialized (9600)");

  // 3. LCD
  lcd.init();
  lcd.backlight();
  Serial.println("LCD Initialized");

  dht.begin();
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  pinMode(BTN_MOVE, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);

  lcd.setCursor(0,0);
  lcd.print("System Ready..");
  delay(1000);
}

/* ================= Loop ================= */
void loop() {
  checkBT();

  bool btnMove = pressed(BTN_MOVE);
  bool btnSelect = pressed(BTN_SELECT);

  if (btnMove) {
    if (!doorEventActive) {
      if (screen == HOME) homeCursor = !homeCursor;
      else if (screen == MENU) menuCursor = (menuCursor + 1) % 4;
      else if (screen == STATUS) statusCursor = (statusCursor + 1) % 3;
      else if (screen == MODE_RUN) modeView = !modeView;
      draw(); 
    }
  }

  if (btnSelect) {
    if (doorEventActive) {
      if (doorCanPlay) {
        doorEventActive = false;
        isDoorChecking = false;
        doorOpenCheckMillis = 0;
        screen = MODE_RUN;
        lastRunMillis = millis();
        draw();
      }
      return; 
    } 
    else {
      if (screen == HOME) screen = (homeCursor == 0) ? STATUS : MENU;
      else if (screen == MENU) {
        if (menuCursor < 3) {
          currentMode = (Mode)menuCursor;
          runState = RUN_RUNNING;
          lastRunMillis = millis();
          elapsedSeconds = 0;
          screen = MODE_RUN;
        } else screen = HOME;
      }
      else if (screen == STATUS) {
        if (statusCursor == 0) {
          if (runState == RUN_RUNNING) runState = RUN_PAUSED;
          else { runState = RUN_RUNNING; lastRunMillis = millis(); }
        } else if (statusCursor == 1) {
          runState = RUN_END;
          elapsedSeconds = 0;
        } else screen = HOME;
      }
      else if (screen == MODE_RUN) screen = HOME;
    }
    draw();
  }

  // 센서 읽기
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temp = t;
  if (!isnan(h)) hum = h;
  
  distanceMM = getStableDistance(); 
  co2Val = analogRead(GAS_PIN);

  // 도어 로직
  if (distanceMM > 100) {
    if (!doorEventActive) {
      if (!isDoorChecking) {
        isDoorChecking = true;
        doorOpenCheckMillis = millis();
      } else {
        if (millis() - doorOpenCheckMillis > 1000) {
          doorEventActive = true;
          doorCanPlay = false;
          draw(); 
        }
      }
    }
  } else {
    isDoorChecking = false;
    doorOpenCheckMillis = 0;
    if (doorEventActive && !doorCanPlay) {
        doorCanPlay = true;
        draw(); 
    }
  }

  if (runState == RUN_RUNNING && !doorEventActive) {
    if (millis() - lastRunMillis >= 1000) {
      elapsedSeconds++;
      lastRunMillis += 1000;
    }
  }

  updateLED();

  // LCD 갱신 (0.3초)
  if (millis() - lastLcdMillis >= 300) {
    lastLcdMillis = millis();
    draw();
  }

  // [서버 전송] 5초 주기
  if (millis() - lastSendMillis >= 5000) {
    lastSendMillis = millis();
    sendDataToServer();
  }
}

/* ================= Functions ================= */

// [핵심] PC 시리얼 모니터와 블루투스 동시 전송
void sendDataToServer() {
  String data = "[ALLMSG]"; 
  data += "ID:0001|";
  data += "MODE:" + String(modeFullName[currentMode]) + "|";
  data += "STATE:" + String(runState==RUN_RUNNING?"RUNNING":runState==RUN_PAUSED?"PAUSED":"END") + "|";
  data += "TIME:" + String(elapsedSeconds / 60) + "min|";
  data += "T:" + String(temp, 1) + "|";
  data += "H:" + String(hum, 0) + "|";
  data += "D:" + String(distanceMM) + "|";
  data += "CO2:" + String(co2Val) + "|";
  data += "HEAT:" + String(digitalRead(LED_RED) ? "ON" : "OFF") + "|";
  data += "COOL:" + String(digitalRead(LED_GREEN) ? "ON" : "OFF") + "|";
  data += "HUM:" + String(digitalRead(LED_YELLOW) ? "ON" : "OFF");
  
  // 1. 블루투스 (서버용) - 줄바꿈 문자(\n)만 추가
  BT.print(data + "\n\n");
  
  // 2. PC 시리얼 모니터 (디버깅용) - println 사용
  Serial.print("[SEND] ");
  Serial.println(data);
}

void updateLED() {
  digitalWrite(LED_RED, LOW); digitalWrite(LED_GREEN, LOW); digitalWrite(LED_YELLOW, LOW);
  if (runState != RUN_RUNNING) return;

  int tMin, tMax, hMin;
  switch (currentMode) {
    case LIQUID: tMin=24; tMax=28; hMin=70; break;
    case DURE:   tMin=22; tMax=26; hMin=50; break;
    case PM:     tMin=18; tMax=22; hMin=40; break;
  }
  if (temp < tMin) digitalWrite(LED_RED, HIGH);
  else if (temp > tMax) digitalWrite(LED_GREEN, HIGH);
  if (hum < hMin) digitalWrite(LED_YELLOW, HIGH);
}

long getStableDistance() {
  const int N = 5; 
  long v[N];
  int cnt = 0;
  for (int i = 0; i < N; i++) {
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long d = pulseIn(ECHO_PIN, HIGH, 20000); 
    if (d > 0) {
      long mm = d * 0.1715; 
      if (mm >= MIN_DIST_MM && mm <= MAX_DIST_MM) v[cnt++] = mm;
    }
    delay(10); 
  }
  if (cnt < 3) return lastStableDistance;
  for (int i = 0; i < cnt - 1; i++) {
    for (int j = i + 1; j < cnt; j++) {
      if (v[i] > v[j]) { long t = v[i]; v[i] = v[j]; v[j] = t; }
    }
  }
  lastStableDistance = v[cnt / 2];
  return lastStableDistance;
}

void checkBT() {
  if (BT.available()) {
    String rx = BT.readStringUntil('\n');
    rx.trim();
    if (rx.length() > 0) {
      Serial.print("[RECV] "); Serial.println(rx); // 디버깅

      bool modeChanged = false;
      if (rx.indexOf("LIQ") != -1) { currentMode = LIQUID; modeChanged = true; }
      else if (rx.indexOf("DURE") != -1) { currentMode = DURE; modeChanged = true; }
      else if (rx.indexOf("PM") != -1) { currentMode = PM; modeChanged = true; }
      
      if (modeChanged) {
        runState = RUN_RUNNING;
        lastRunMillis = millis();
        elapsedSeconds = 0;
        screen = MODE_RUN;
      }
      if (rx.indexOf("RUN") != -1 && !modeChanged) {
        runState = RUN_RUNNING;
        lastRunMillis = millis();
        screen = MODE_RUN;
      }
      else if (rx.indexOf("PAUSE") != -1) runState = RUN_PAUSED;
      else if (rx.indexOf("END") != -1) {
        runState = RUN_END;
        elapsedSeconds = 0;
      }
      draw();
    }
  }
}

bool pressed(int pin) {
  static unsigned long last[40];
  if (digitalRead(pin) == LOW) {
    if (millis() - last[pin] > 500) {
      last[pin] = millis();
      return true;
    }
  }
  return false;
}

void draw() {
  lcd.clear();
  if (doorEventActive) {
    lcd.setCursor(0,0); lcd.print("DOOR OPEN!");
    lcd.setCursor(0,1); lcd.print(doorCanPlay ? "> RESUME" : "  WAIT...");
    return;
  }
  if (screen == HOME) {
    lcd.setCursor(0,0); lcd.print(homeCursor == 0 ? ">1.Status" : " 1.Status");
    lcd.setCursor(0,1); lcd.print(homeCursor == 1 ? ">2.Menu"   : " 2.Menu");
  }
  else if (screen == MENU) {
    const char* items[] = {"1.Liquid","2.Dure","3.PM","4.BACK"};
    int top = (menuCursor/2)*2;
    for (int i=0;i<2;i++) {
      lcd.setCursor(0,i); lcd.print(menuCursor == top+i ? ">" : " "); lcd.print(items[top+i]);
    }
  }
  else if (screen == STATUS) {
    lcd.setCursor(0,0);
    lcd.print(modeFullName[currentMode]); lcd.print(" ");
    lcd.print(runState == RUN_RUNNING ? "RUN" : runState == RUN_PAUSED ? "PAU" : "END");
    lcd.setCursor(0,1);
    if (statusCursor == 0) lcd.print(runState == RUN_PAUSED ? ">1.Run" : ">1.Pause");
    else if (statusCursor == 1) lcd.print(">2.END");
    else lcd.print(">3.BACK");
  }
  else if (screen == MODE_RUN) {
    if (modeView == 0) {
      lcd.setCursor(0,0); lcd.print(modeTags[currentMode]);
      lcd.print(" T:"); lcd.print((int)temp); lcd.print(" H:"); lcd.print((int)hum);
      lcd.setCursor(0,1); lcd.print("D:"); lcd.print(distanceMM); lcd.print(" ");
      if (elapsedSeconds < CO2_WARMUP_SEC) {
         if (millis() - co2BlinkMillis > 500) {
           co2BlinkMillis = millis(); co2BlinkStep = (co2BlinkStep + 1) % 3;
         }
         lcd.print("CO2"); lcd.print(co2BlinkStep==0?".":co2BlinkStep==1?"..":"...");
      } else { lcd.print("C:"); lcd.print(co2Val); }
    } else {
      lcd.setCursor(0,0); lcd.print("RUN TIME:");
      lcd.setCursor(0,1); printRunTimeLCD();
    }
  }
}

void printRunTimeLCD() {
  unsigned long sec = elapsedSeconds;
  unsigned long mins = sec / 60UL;
  unsigned long hours = mins / 60UL;
  mins %= 60UL;
  if(hours > 0) { lcd.print(hours); lcd.print("h "); }
  lcd.print(mins); lcd.print("m ");
  lcd.print(sec % 60); lcd.print("s");
}