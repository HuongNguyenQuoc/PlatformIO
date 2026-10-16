#include <Arduino.h>
#include <LiquidCrystal.h>
#include <SoftwareSerial.h>

// Khoi tao LCD 1602 theo cac chan:
// LiquidCrystal(RS, Enable, D4, D5, D6, D7)
// Khoi tao LCD 1602 theo cac chan da hoat dong tot
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

// Cong Serial thu 2 noi sang ESP32-CAM qua module BSS138:
// D8 nhan (tu ESP IO13, kenh HV2-LV2), D9 gui (sang ESP IO14, kenh HV1-LV1)
SoftwareSerial espSerial(8, 9);

const int SOIL_PIN = A0; // Chan doc Analog cua cam bien do am dat
const int RELAY_PIN = 7; // Chan dieu khien relay may bom (IN cua module relay)

// Module relay 5V pho bien kich muc LOW (IN = LOW thi relay dong).
// Neu relay cua ban kich muc HIGH thi doi thanh HIGH.
const int RELAY_ACTIVE = LOW;

const int FAN_PIN = 6;             // Chan dieu khien relay quat (IN2 neu dung module relay 2 kenh)
const int FAN_RELAY_ACTIVE = LOW;  // Kich muc LOW giong relay bom

// Quat thong gio theo ket qua AI: la kho nhanh hon, giam cac benh nam ua am (moc suong, dom la...).
// BENH: bat 10 phut / tat 20 phut. NGHI: bat 5 phut / tat 25 phut. Con lai: tat.
// Muon test nhanh thi tam doi thanh vai giay, vd 10000UL va 20000UL.
const unsigned long FAN_DISEASE_ON_MS = 10UL * 60000;
const unsigned long FAN_DISEASE_OFF_MS = 20UL * 60000;
const unsigned long FAN_SUSPECT_ON_MS = 5UL * 60000;
const unsigned long FAN_SUSPECT_OFF_MS = 25UL * 60000;

// Gia tri hieu chuan mac dinh (ban se cap nhat sau khi test thuc te)
int airValue = 970;   // Gia tri RAW khi cam bien o ngoai khong khi kho (0%)
int waterValue = 746; // Gia tri RAW khi nhung cam bien vao nuoc (100%)

const int SAMPLE_COUNT = 10; // So lan doc de lay trung binh

// Nguong tuoi: duoi LOW thi bat dau tuoi, tren HIGH thi dung tuoi.
// Khoang cach 15% de moi dot tuoi dam roi de dat kho bot, thay vi tuoi lat nhat lien tuc.
const int MOISTURE_LOW = 50;
const int MOISTURE_HIGH = 65;

// RAW thap hon muc nay nghia la cam bien mat nguon / tuot day (nhu luc truoc doc 9-103)
const int SENSOR_FAULT_RAW = 600;

// Luu luong bom do thuc te: cho bom chay 30 giay vao coc co vach, lay so ml nhan 2.
// Bom chim mini 3-6V da gan ong thuong duoc khoang 1 lit/phut.
const unsigned long PUMP_FLOW_ML_PER_MIN = 1000;
const unsigned long WATER_PER_PULSE_ML = 100; // Luong nuoc moi lan relay dong
const unsigned long PUMP_ON_MS = WATER_PER_PULSE_ML * 60000UL / PUMP_FLOW_ML_PER_MIN; // 100 ml -> 6 giay

// Moi lan chi bom ngan roi cho nuoc ngam xuong toi cam bien, tranh bom tran chau khi cam bien phan ung cham.
// Muon test nhanh thi tam doi SOAK_MS = 5000UL, REST_MS = 60000UL.
const unsigned long SOAK_MS = 60000UL;        // Cho nuoc ngam giua 2 lan bom (60 giay)
const unsigned long DRY_CONFIRM_MS = 10000UL; // Phai kho lien tuc 10 giay moi bat dau tuoi, tranh 1 lan doc nhieu
const int MAX_PULSES = 8;                     // Toi da 8 lan bom (~800 ml) moi dot tuoi
const unsigned long REST_MS = 30UL * 60000;   // Bom du 8 lan ma chua du am thi nghi 30 phut cho nuoc lan deu chau

// Khoa bom (den khi bam RESET Nano) khi bom du 8 lan ma do am tang chua toi MIN_RISE:
// het nuoc, tuot ong hoac bom hong. Hoac khi MAX_INCOMPLETE_CYCLES dot lien tiep khong du am, tranh ngap chau.
const int MIN_RISE = 5;
const int MAX_INCOMPLETE_CYCLES = 3;

bool watering = false;              // Dang trong dot tuoi (do am chua len toi MOISTURE_HIGH)
bool pumpOn = false;                // May bom dang chay
bool pumpLocked = false;            // Bom bi khoa vi nghi het nuoc / hong bom
unsigned long pumpStartTime = 0;    // Thoi diem bat bom gan nhat
unsigned long pumpStopTime = 0;     // Thoi diem tat bom gan nhat
unsigned long drySince = 0;         // Thoi diem do am bat dau xuong duoi MOISTURE_LOW
int pulseCount = 0;                 // So lan da bom trong dot tuoi hien tai
int cycleStartMoisture = 0;         // Do am luc bat dau dot tuoi
int incompleteCycles = 0;           // So dot lien tiep bom du MAX_PULSES ma chua du am
bool resting = false;               // Dang nghi REST_MS sau 1 dot chua du am
unsigned long restStart = 0;

bool fanOn = false;                 // Quat dang chay
byte fanLevel = 0;                  // Muc thong gio theo ket qua AI gan nhat: 0 = tat, 1 = nghi benh, 2 = benh
unsigned long fanCycleStart = 0;    // Thoi diem bat dau chu ky thong gio hien tai

// Ket qua AI, moi dong dang "AI:<toi da 16 ky tu>\n", vd "AI:BENH 99%". Gui "AI:" (rong) de xoa ket qua.
// Den tu ESP32-CAM (app gui len ESP, ESP chuyen xuong qua D8) hoac tu laptop (ai/service.py, cap USB).
// Ca 2 nguon deu gui lai ket qua moi 60 giay, nen qua AI_TIMEOUT_MS ma khong nghe gi la da mat ket noi.
const unsigned long AI_SHOW_MS = 10000;     // Dong 2 LCD hien ket qua AI 10 giay...
const unsigned long PUMP_SHOW_MS = 3000;    // ...roi hien trang thai bom 3 giay, lap lai
const unsigned long AI_TIMEOUT_MS = 180000; // 3 phut
char aiText[17] = "";                       // Noi dung AI dang hien (16 ky tu + '\0')
bool hasAiResult = false;
unsigned long lastAiTime = 0;               // Thoi diem nhan dong "AI:" gan nhat

// Gui do am cho ESP32-CAM de app xem duoc (ESP dua len trang /status)
const unsigned long SOIL_REPORT_MS = 5000;
unsigned long lastSoilReport = 0;

// Bo dem cho 1 dong lenh dang nhan do. Moi cong Serial 1 bo dem rieng de 2 nguon khong tron lan nhau.
struct LineBuffer {
  char data[24];
  byte len;
};
LineBuffer usbLine = {"", 0};
LineBuffer espLine = {"", 0};

// Doc A0 nhieu lan roi lay trung binh de so do on dinh hon
int readSoilAverage() {
  long sum = 0; // Dung long vi 10 x 1023 vuot qua gioi han an toan cua int
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(SOIL_PIN);
    delay(10); // Nghi ngan giua cac lan doc
  }
  return sum / SAMPLE_COUNT;
}

// Relay dong/ngat gay nhieu dien, LCD de bi lech du lieu va hien ky tu rac.
// Cho nhieu qua roi khoi tao lai LCD; loop() se ve lai toan bo noi dung ngay sau do.
void resetLcdAfterRelay() {
  delay(50);
  lcd.begin(16, 2);
}

void setPump(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ACTIVE : !RELAY_ACTIVE);
  if (on && !pumpOn) {
    pumpStartTime = millis();
  } else if (!on && pumpOn) {
    pumpStopTime = millis();
  }
  if (on != pumpOn) {
    resetLcdAfterRelay();
  }
  pumpOn = on;
}

void setFan(bool on) {
  digitalWrite(FAN_PIN, on ? FAN_RELAY_ACTIVE : !FAN_RELAY_ACTIVE);
  if (on != fanOn) {
    resetLcdAfterRelay();
  }
  fanOn = on;
}

// Quat bat hay tat luc nay: dang o phan "bat" hay phan "tat" cua chu ky thong gio
bool fanShouldRun(unsigned long now) {
  if (fanLevel == 0) {
    return false;
  }
  unsigned long onMs = fanLevel == 2 ? FAN_DISEASE_ON_MS : FAN_SUSPECT_ON_MS;
  unsigned long offMs = fanLevel == 2 ? FAN_DISEASE_OFF_MS : FAN_SUSPECT_OFF_MS;
  return (now - fanCycleStart) % (onMs + offMs) < onMs;
}

// reply: cong Serial da gui dong nay, de bao lai dung nguon "AI OK: ..."
void handleSerialLine(const char *line, Stream &reply) {
  if (strncmp(line, "AI:", 3) != 0) {
    return; // Khong phai lenh AI thi bo qua
  }
  strncpy(aiText, line + 3, sizeof(aiText) - 1);
  aiText[sizeof(aiText) - 1] = '\0';
  hasAiResult = aiText[0] != '\0';
  lastAiTime = millis();

  // Muc thong gio theo ket qua moi. Chi bat dau lai chu ky khi muc thay doi:
  // ESP gui lai cung 1 ket qua moi 60 giay, neu lan nao cung bat dau lai thi quat se khong bao gio tat.
  byte level = strncmp(aiText, "BENH", 4) == 0 ? 2 : strncmp(aiText, "NGHI", 4) == 0 ? 1 : 0;
  if (level != fanLevel) {
    fanLevel = level;
    fanCycleStart = lastAiTime;
  }

  reply.print("AI OK: ");
  reply.println(aiText);
}

// Lay het ky tu dang cho trong bo dem cua 1 cong Serial, gap '\n' thi xu ly ca dong. Khong chan loop().
void readLines(Stream &port, LineBuffer &line) {
  while (port.available() > 0) {
    char c = port.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line.data[line.len] = '\0';
      handleSerialLine(line.data, port);
      line.len = 0;
    } else if (line.len < sizeof(line.data) - 1) {
      line.data[line.len++] = c;
    }
  }
}

// Gui 1 dong "SOIL:<do am %>,<raw>,<bom 0/1>,<loi cam bien 0/1>,<quat 0/1>" cho ESP32-CAM, vd "SOIL:45,812,0,0,1"
void reportSoil(int moisturePercent, int rawValue, bool sensorFault) {
  espSerial.print("SOIL:");
  espSerial.print(moisturePercent);
  espSerial.print(',');
  espSerial.print(rawValue);
  espSerial.print(',');
  espSerial.print(pumpOn ? 1 : 0);
  espSerial.print(',');
  espSerial.print(sensorFault ? 1 : 0);
  espSerial.print(',');
  espSerial.println(fanOn ? 1 : 0);
}

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);

  // Tat bom va quat truoc roi moi dat OUTPUT, de relay khong bi dong thoang qua luc khoi dong
  digitalWrite(RELAY_PIN, !RELAY_ACTIVE);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(FAN_PIN, !FAN_RELAY_ACTIVE);
  pinMode(FAN_PIN, OUTPUT);

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
  // 0. Nhan ket qua AI tu laptop (cap USB) va tu ESP32-CAM (D8), neu co
  readLines(Serial, usbLine);
  readLines(espSerial, espLine);

  // 1. Doc gia tri Analog (0 - 1023), lay trung binh SAMPLE_COUNT lan
  int rawValue = readSoilAverage();
  bool sensorFault = rawValue < SENSOR_FAULT_RAW;

  // 2. Quy doi ra % do am dat (0% - 100%)
  // Luu y: Cam bien dien dung cang kho thi gia tri cang cao, cang uot gia tri cang thap
  int moisturePercent = map(rawValue, airValue, waterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  // 3. Quyet dinh tuoi
  unsigned long now = millis();
  if (sensorFault || moisturePercent >= MOISTURE_LOW) {
    drySince = now; // Chua kho thi dem lai tu dau
  }
  bool restDone = !resting || now - restStart >= REST_MS;

  if (sensorFault || pumpLocked) {
    // Cam bien loi thi khong tin so do, bom bi khoa thi cho nguoi kiem tra: tat bom cho an toan
    watering = false;
  } else if (watering && moisturePercent >= MOISTURE_HIGH) {
    watering = false; // Du am, ket thuc dot tuoi
    incompleteCycles = 0;
  } else if (!watering && restDone && now - drySince >= DRY_CONFIRM_MS) {
    watering = true; // Bat dau dot tuoi moi
    resting = false;
    pulseCount = 0;
    cycleStartMoisture = moisturePercent;
  }

  if (pumpOn) {
    // Bom du PUMP_ON_MS hoac khong can tuoi nua thi tat
    if (!watering || now - pumpStartTime >= PUMP_ON_MS) {
      setPump(false);
    }
  } else if (watering && now - pumpStopTime >= SOAK_MS) {
    if (pulseCount < MAX_PULSES) {
      // Da cho nuoc tham du lau ma van kho thi bom them 1 lan
      setPump(true);
      pulseCount++;
    } else {
      // Bom du MAX_PULSES lan, ngam xong ma van chua du am
      watering = false;
      incompleteCycles++;
      if (moisturePercent - cycleStartMoisture < MIN_RISE || incompleteCycles >= MAX_INCOMPLETE_CYCLES) {
        pumpLocked = true;
      } else {
        resting = true;
        restStart = now;
      }
    }
  }

  // Quat thong gio theo chu ky cua ket qua AI gan nhat (mat ket noi van giu muc cu: benh khong tu het)
  setFan(fanShouldRun(now));

  // 4. Hien thi len LCD1602
  // Dong 1: % Do am dat + trang thai quat o cot 11 (hoac bao loi cam bien)
  lcd.setCursor(0, 0);
  if (sensorFault) {
    lcd.print("Loi cam bien!   ");
  } else {
    lcd.print("Do am: ");
    lcd.print(moisturePercent);
    lcd.print("%    ");
    lcd.setCursor(11, 0);
    lcd.print(fanOn ? "Q:ON " : "Q:OFF");
  }

  // Dong 2: Trang thai bom + gia tri Raw de quan sat hieu chuan,
  // luan phien voi ket qua AI (AI_SHOW_MS / PUMP_SHOW_MS) neu da nhan duoc ket qua
  lcd.setCursor(0, 1);
  if (hasAiResult && now % (AI_SHOW_MS + PUMP_SHOW_MS) < AI_SHOW_MS) {
    const char *text = now - lastAiTime > AI_TIMEOUT_MS ? "MAT KET NOI ESP" : aiText;
    lcd.print(text);
    for (int i = strlen(text); i < 16; i++) {
      lcd.print(' '); // Xoa phan chu cu con sot lai
    }
  } else {
    // CHO = dang trong dot tuoi, doi nuoc ngam; LOI = bom bi khoa
    lcd.print(pumpLocked ? "Bom:LOI " : pumpOn ? "Bom:ON  " : watering ? "Bom:CHO " : "Bom:OFF ");
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
  Serial.print("  Quat: ");
  Serial.print(fanOn ? "ON" : "OFF");
  if (watering) {
    Serial.print("  Lan bom: ");
    Serial.print(pulseCount);
    Serial.print('/');
    Serial.print(MAX_PULSES);
  }
  if (sensorFault) {
    Serial.print("  [LOI CAM BIEN]");
  }
  if (pumpLocked) {
    Serial.print("  [KHOA BOM: kiem tra nuoc, ong, bom roi bam RESET]");
  }
  Serial.println();

  // 6. Gui do am cho ESP32-CAM moi SOIL_REPORT_MS
  if (now - lastSoilReport >= SOIL_REPORT_MS) {
    lastSoilReport = now;
    reportSoil(moisturePercent, rawValue, sensorFault);
  }

  delay(500); // Cap nhat moi 0.5 giay
}
