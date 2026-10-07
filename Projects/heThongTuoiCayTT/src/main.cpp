#include <Arduino.h>
#include <LiquidCrystal.h>

// Khoi tao LCD 1602 theo cac chan:
// LiquidCrystal(RS, Enable, D4, D5, D6, D7)
// Khoi tao LCD 1602 theo cac chan da hoat dong tot
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int SOIL_PIN = A0; // Chan doc Analog cua cam bien do am dat
const int RELAY_PIN = 7; // Chan dieu khien relay may bom (IN cua module relay)

// Module relay 5V pho bien kich muc LOW (IN = LOW thi relay dong).
// Neu relay cua ban kich muc HIGH thi doi thanh HIGH.
const int RELAY_ACTIVE = LOW;

// Gia tri hieu chuan mac dinh (ban se cap nhat sau khi test thuc te)
int airValue = 970;   // Gia tri RAW khi cam bien o ngoai khong khi kho (0%)
int waterValue = 746; // Gia tri RAW khi nhung cam bien vao nuoc (100%)

const int SAMPLE_COUNT = 10; // So lan doc de lay trung binh

// Nguong tuoi: duoi LOW thi bat dau tuoi, tren HIGH thi dung tuoi
const int MOISTURE_LOW = 35;
const int MOISTURE_HIGH = 60;

// RAW thap hon muc nay nghia la cam bien mat nguon / tuot day (nhu luc truoc doc 9-103)
const int SENSOR_FAULT_RAW = 600;

// Moi lan chi bom ngan roi cho nuoc tham, tranh bom tran chau khi cam bien phan ung cham
const unsigned long PUMP_ON_MS = 3000;  // Thoi gian bom moi lan (3 giay)
const unsigned long SOAK_MS = 5000;     // Thoi gian cho nuoc tham giua 2 lan bom (5 giay de test, chau that nen 30-60 giay)

bool watering = false;              // Dang trong chu ky tuoi (do am chua len toi MOISTURE_HIGH)
bool pumpOn = false;                // May bom dang chay
unsigned long pumpStartTime = 0;    // Thoi diem bat bom gan nhat
unsigned long pumpStopTime = 0;     // Thoi diem tat bom gan nhat

// Ket qua AI tu laptop (ai/service.py gui qua cap USB), moi dong dang "AI:<toi da 13 ky tu>\n"
// vd "AI:Benh 97% TLB". Gui "AI:" (rong) de xoa ket qua.
const unsigned long AI_TOGGLE_MS = 3000; // Dong 2 LCD luan phien bom <-> AI moi 3 giay
char aiText[14] = "";                    // Noi dung AI dang hien (13 ky tu + '\0')
bool hasAiResult = false;
char serialLine[24];                     // Dong lenh dang nhan do qua Serial
byte serialLen = 0;

// Doc A0 nhieu lan roi lay trung binh de so do on dinh hon
int readSoilAverage() {
  long sum = 0; // Dung long vi 10 x 1023 vuot qua gioi han an toan cua int
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(SOIL_PIN);
    delay(10); // Nghi ngan giua cac lan doc
  }
  return sum / SAMPLE_COUNT;
}

void setPump(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ACTIVE : !RELAY_ACTIVE);
  if (on && !pumpOn) {
    pumpStartTime = millis();
  } else if (!on && pumpOn) {
    pumpStopTime = millis();
  }
  if (on != pumpOn) {
    // Relay dong/ngat gay nhieu dien, LCD de bi lech du lieu va hien ky tu rac.
    // Cho nhieu qua roi khoi tao lai LCD; loop() se ve lai toan bo noi dung ngay sau do.
    delay(50);
    lcd.begin(16, 2);
  }
  pumpOn = on;
}

void handleSerialLine(const char *line) {
  if (strncmp(line, "AI:", 3) != 0) {
    return; // Khong phai lenh AI thi bo qua
  }
  strncpy(aiText, line + 3, sizeof(aiText) - 1);
  aiText[sizeof(aiText) - 1] = '\0';
  hasAiResult = aiText[0] != '\0';
  Serial.print("AI OK: "); // Bao lai cho laptop biet da nhan
  Serial.println(aiText);
}

// Lay het ky tu dang cho trong bo dem Serial, gap '\n' thi xu ly ca dong. Khong chan loop().
void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      serialLine[serialLen] = '\0';
      handleSerialLine(serialLine);
      serialLen = 0;
    } else if (serialLen < sizeof(serialLine) - 1) {
      serialLine[serialLen++] = c;
    }
  }
}

void setup() {
  Serial.begin(9600);

  // Tat bom truoc roi moi dat OUTPUT, de relay khong bi dong thoang qua luc khoi dong
  digitalWrite(RELAY_PIN, !RELAY_ACTIVE);
  pinMode(RELAY_PIN, OUTPUT);

  lcd.begin(16, 2);
  lcd.clear();

  // Man hinh chao khoi dong
  lcd.setCursor(0, 0);
  lcd.print("He Thong Tuoi");
  lcd.setCursor(0, 1);
  lcd.print("Do Am Dat v2.0");
  delay(1500);
  lcd.clear();
}

void loop() {
  // 0. Nhan ket qua AI tu laptop (neu co)
  readSerialCommands();

  // 1. Doc gia tri Analog (0 - 1023), lay trung binh SAMPLE_COUNT lan
  int rawValue = readSoilAverage();
  bool sensorFault = rawValue < SENSOR_FAULT_RAW;

  // 2. Quy doi ra % do am dat (0% - 100%)
  // Luu y: Cam bien dien dung cang kho thi gia tri cang cao, cang uot gia tri cang thap
  int moisturePercent = map(rawValue, airValue, waterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  // 3. Quyet dinh tuoi
  if (sensorFault) {
    // Cam bien loi thi khong tin so do, tat bom cho an toan
    watering = false;
  } else if (!watering && moisturePercent < MOISTURE_LOW) {
    watering = true;
  } else if (watering && moisturePercent >= MOISTURE_HIGH) {
    watering = false;
  }

  unsigned long now = millis();
  if (pumpOn) {
    // Bom du PUMP_ON_MS hoac khong can tuoi nua thi tat
    if (!watering || now - pumpStartTime >= PUMP_ON_MS) {
      setPump(false);
    }
  } else if (watering && now - pumpStopTime >= SOAK_MS) {
    // Da cho nuoc tham du lau ma van kho thi bom them 1 lan
    setPump(true);
  }

  // 4. Hien thi len LCD1602
  // Dong 1: % Do am dat (hoac bao loi cam bien)
  lcd.setCursor(0, 0);
  if (sensorFault) {
    lcd.print("Loi cam bien!   ");
  } else {
    lcd.print("Do am: ");
    lcd.print(moisturePercent);
    lcd.print("%       ");
  }

  // Dong 2: Trang thai bom + gia tri Raw de quan sat hieu chuan,
  // luan phien voi ket qua AI moi AI_TOGGLE_MS neu laptop da gui ket qua
  lcd.setCursor(0, 1);
  if (hasAiResult && (millis() / AI_TOGGLE_MS) % 2 == 1) {
    lcd.print("AI:");
    lcd.print(aiText);
    for (int i = 3 + strlen(aiText); i < 16; i++) {
      lcd.print(' '); // Xoa phan chu cu con sot lai
    }
  } else {
    lcd.print(pumpOn ? "Bom:ON  " : "Bom:OFF ");
    lcd.print("R:");
    lcd.print(rawValue);
    lcd.print("    ");
  }

  // 5. In ra Serial Monitor
  Serial.print("Raw: ");
  Serial.print(rawValue);
  Serial.print("  -->  Do am: ");
  Serial.print(moisturePercent);
  Serial.print("%  Bom: ");
  Serial.print(pumpOn ? "ON" : "OFF");
  if (sensorFault) {
    Serial.print("  [LOI CAM BIEN]");
  }
  Serial.println();

  delay(500); // Cap nhat moi 0.5 giay
}
