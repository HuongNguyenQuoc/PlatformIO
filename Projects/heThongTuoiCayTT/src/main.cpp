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

const int MIST_PIN = 6;             // Chan dieu khien relay may phun suong 5V (IN2 neu dung module relay 2 kenh)
const int MIST_RELAY_ACTIVE = LOW;  // Kich muc LOW giong relay bom

// Mach phun JY-M36A0 khong nho trang thai: moi lan duoc cap dien no o trang thai TAT, phai bam nut S1 1 lan moi phun.
// D10 noi qua diode 1N4007 (vach ve phia Nano) toi chan tin hieu cua S1. Keo D10 xuong LOW = bam S1;
// luc thuong D10 tha noi (INPUT), diode chan nen Nano khong bao gio day dien vao mach phun.
const int MIST_BTN_PIN = 10;
const unsigned long MIST_BOOT_MS = 500;   // Relay cap dien xong doi mach phun khoi dong roi moi bam
const unsigned long MIST_PRESS_MS = 200;  // Giu nut 0,2 giay

// Phun suong theo gio that (ESP32-CAM gui xuong) va che do ESP gui theo tinh trang la AI bao.
// Chi phun ban ngay de la kip kho truoc toi: la uot qua dem lam benh nam, vi khuan lan nhanh.
// Nhip phun tinh theo dong ho nen luon phun dung luc :00, :30 (hoac :00, :15, :30, :45).
const int MIST_NORMAL = 1;                   // La khoe, benh virus, hoac chua co ket qua AI
const int MIST_STOP = 2;                     // La benh nam / vi khuan, hoac nghi benh: khong phun
const int MIST_BOOST = 3;                    // Chac chan nhen do (so am): phun day hon
const long MIST_ON_SEC = 30;                 // Moi lan phun 30 giay...
const long MIST_EVERY_SEC = 30L * 60;        // ...30 phut 1 lan
const long MIST_BOOST_EVERY_SEC = 15L * 60;  // ...hoac 15 phut 1 lan khi MIST_BOOST
const long MIST_START_SEC = 8L * 3600;       // Chi phun tu 8h...
const long MIST_END_SEC = 16L * 3600;        // ...den truoc 16h
// Muon test nhanh thi tam doi MIST_EVERY_SEC = 120 (phun 30 giay moi 2 phut)

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

const unsigned long CONTROL_MS = 500; // Doc cam bien, quyet dinh tuoi / phun moi 0.5 giay

int rawValue = 0;                   // Lan doc cam bien gan nhat
int moisturePercent = 0;
bool sensorFault = false;
unsigned long lastControl = 0;

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

bool mistOn = false;                // May phun suong dang chay
unsigned long mistOnSince = 0;      // millis() luc relay phun dong
byte mistPress = 0;                 // Bam S1 sau khi cap dien: 0 = chua bam, 1 = dang giu nut, 2 = xong
int mistMode = MIST_NORMAL;         // Che do phun ESP gui gan nhat ("MIST:<1-3>")

// Gio that: ESP gui "TIME:hh:mm:ss" moi 60 giay, giua 2 lan Nano tu dem bang millis().
// ESP mat gio (khoi dong lai luc khong co Internet) thi Nano van tu dem tiep toi da TIME_HOLD_MS.
const unsigned long TIME_HOLD_MS = 12UL * 3600000; // 12 gio: dong ho Nano chi lech vai phut
long timeBaseSec = -1;              // Giay trong ngay luc nhan dong TIME gan nhat, -1 = chua biet gio
unsigned long timeBaseMs = 0;       // millis() luc nhan dong do

// Ket qua AI moi, dang "AI:<dong la>|<dong qua>", vd "AI:La ca chua: Moc suong 97%|Qua ca chua: Tuoi 92%".
// Den tu ESP32-CAM (app gui len ESP, ESP chuyen xuong qua D8) hoac tu laptop (ai/service.py, cap USB).
// Moi dong AI: la 1 lan chup moi: LCD hien 2 dong ket qua AI_SHOW_MS roi quay ve man hinh chinh.
const unsigned long AI_SHOW_MS = 20000;
const byte AI_TEXT_LEN = 40;        // Moi dong toi da 40 ky tu (khop RESULT_MAX_LEN cua ESP32-CAM)
char leafText[AI_TEXT_LEN + 1] = "";
char fruitText[AI_TEXT_LEN + 1] = "";
bool showingAi = false;             // Dang hien ket qua AI thay cho man hinh chinh
unsigned long aiShowStart = 0;

// Dong dai hon 16 ky tu thi chay chu: dung o dau SCROLL_HOLD_MS, moi SCROLL_STEP_MS dich 1 ky tu, dung o cuoi
const unsigned long SCROLL_HOLD_MS = 1500;
const unsigned long SCROLL_STEP_MS = 350;

// Noi dung dang hien tren LCD: chi ghi lai dong nao doi, khong ghi lien tuc ca 2 dong
char shown[2][17] = {"", ""};

// Gui do am cho ESP32-CAM de app xem duoc (ESP dua len trang /status)
const unsigned long SOIL_REPORT_MS = 5000;
unsigned long lastSoilReport = 0;

// Bo dem cho 1 dong lenh dang nhan do. Moi cong Serial 1 bo dem rieng de 2 nguon khong tron lan nhau.
// Du cho dong AI dai nhat: "AI:" + 40 + "|" + 40 ky tu.
struct LineBuffer {
  char data[88];
  byte len;
};
LineBuffer usbLine = {"", 0};
LineBuffer espLine = {"", 0};

void readSerialPorts();

// Cho ms mili giay ma van nhan lenh tu USB va ESP. Bo dem SoftwareSerial chi 64 byte,
// dung delay() lau thi dong AI dai (~85 ky tu) bi tran, mat chu.
void waitListening(unsigned long ms) {
  unsigned long start = millis();
  do {
    readSerialPorts();
  } while (millis() - start < ms);
}

// Doc A0 nhieu lan roi lay trung binh de so do on dinh hon
int readSoilAverage() {
  long sum = 0; // Dung long vi 10 x 1023 vuot qua gioi han an toan cua int
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += analogRead(SOIL_PIN);
    waitListening(10); // Nghi ngan giua cac lan doc
  }
  return sum / SAMPLE_COUNT;
}

// Relay dong/ngat gay nhieu dien, LCD de bi lech du lieu va hien ky tu rac.
// Cho nhieu qua roi khoi tao lai LCD; updateLcd() se ve lai toan bo noi dung ngay sau do.
// Bom / phun dang chay van tiep tuc gay nhieu, nen trong luc do khoi tao lai LCD moi LCD_REFRESH_MS
// de ky tu rac tu het sau toi da vai giay (man hinh chop nhe 1 lan).
const unsigned long LCD_REFRESH_MS = 5000;
unsigned long lastLcdReset = 0;

void resetLcdAfterRelay() {
  waitListening(50);
  lcd.begin(16, 2);
  shown[0][0] = '\0';
  shown[1][0] = '\0';
  lastLcdReset = millis();
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

// Bat: relay cap dien, updateMistButton() bam S1 sau MIST_BOOT_MS.
// Tat: ngat relay, mach phun mat dien nen lan sau lai bat dau tu TAT: Nano luon biet mach dang bat hay tat.
void setMist(bool on) {
  digitalWrite(MIST_PIN, on ? MIST_RELAY_ACTIVE : !MIST_RELAY_ACTIVE);
  if (on != mistOn) {
    mistOnSince = millis();
    mistPress = 0;
    pinMode(MIST_BTN_PIN, INPUT);  // Tat giua luc dang bam thi nha nut ngay
    resetLcdAfterRelay();
  }
  mistOn = on;
}

// Goi lien tuc trong loop(): giu S1 dung MIST_PRESS_MS du control() co dung doc cam bien
void updateMistButton(unsigned long now) {
  if (!mistOn || mistPress == 2) {
    return;
  }
  if (mistPress == 0 && now - mistOnSince >= MIST_BOOT_MS) {
    digitalWrite(MIST_BTN_PIN, LOW);  // Dat LOW truoc khi chuyen OUTPUT de khong loe HIGH
    pinMode(MIST_BTN_PIN, OUTPUT);
    mistPress = 1;
  } else if (mistPress == 1 && now - mistOnSince >= MIST_BOOT_MS + MIST_PRESS_MS) {
    pinMode(MIST_BTN_PIN, INPUT);
    mistPress = 2;
    // Mach phun bat dau rung sieu am luc bam S1, sau lan reset LCD trong setMist(): reset lai lan nua
    resetLcdAfterRelay();
  }
}

// Giay trong ngay (0-86399) luc nay, -1 neu chua biet gio
long secondOfDay(unsigned long now) {
  if (timeBaseSec < 0 || now - timeBaseMs > TIME_HOLD_MS) {
    return -1;
  }
  return (timeBaseSec + (long)((now - timeBaseMs) / 1000)) % 86400L;
}

// Luc nay co phun khong: dung khung gio, dung nhip, va la khong bi benh nam / vi khuan
bool mistShouldRun(unsigned long now) {
  long sec = secondOfDay(now);
  if (sec < 0 || mistMode == MIST_STOP || sec < MIST_START_SEC || sec >= MIST_END_SEC) {
    return false; // Chua biet gio thi khong phun, tranh phun nham ban dem
  }
  long every = mistMode == MIST_BOOST ? MIST_BOOST_EVERY_SEC : MIST_EVERY_SEC;
  return sec % every < MIST_ON_SEC;
}

// Chep toi da AI_TEXT_LEN ky tu
void copyText(char *dst, const char *src, size_t len) {
  len = min(len, (size_t)AI_TEXT_LEN);
  memcpy(dst, src, len);
  dst[len] = '\0';
}

// Cac lenh nhan duoc (go tay tren Serial Monitor cung duoc, de thu):
//   AI:<dong la>|<dong qua>   ket qua moi, LCD hien AI_SHOW_MS. Tra loi "AI OK" de ESP khoi gui lai. "AI:" = thoi hien
//   MIST:<1-3>                che do phun suong (MIST_*)
//   TIME:hh:mm:ss             gio that
// reply: cong Serial da gui dong nay, de tra loi dung nguon
void handleSerialLine(const char *line, Stream &reply) {
  if (strncmp(line, "AI:", 3) == 0) {
    const char *text = line + 3;
    const char *bar = strchr(text, '|');
    copyText(leafText, text, bar ? (size_t)(bar - text) : strlen(text));
    copyText(fruitText, bar ? bar + 1 : "", bar ? strlen(bar + 1) : 0);
    showingAi = text[0] != '\0';
    aiShowStart = millis();
    reply.println("AI OK");
  } else if (strncmp(line, "MIST:", 5) == 0) {
    int mode = atoi(line + 5);
    if (mode >= MIST_NORMAL && mode <= MIST_BOOST) {
      mistMode = mode;
    }
  } else if (strncmp(line, "TIME:", 5) == 0) {
    int h, m, s;
    if (sscanf(line + 5, "%d:%d:%d", &h, &m, &s) == 3 && h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 60) {
      timeBaseSec = h * 3600L + m * 60L + s;
      timeBaseMs = millis();
    }
  }
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

// Nhan lenh tu laptop (cap USB) va tu ESP32-CAM (D8), neu co
void readSerialPorts() {
  readLines(Serial, usbLine);
  readLines(espSerial, espLine);
}

// Gui 1 dong "SOIL:<do am %>,<raw>,<bom 0/1>,<loi cam bien 0/1>,<phun suong 0/1>" cho ESP32-CAM, vd "SOIL:45,812,0,0,1"
void reportSoil() {
  espSerial.print("SOIL:");
  espSerial.print(moisturePercent);
  espSerial.print(',');
  espSerial.print(rawValue);
  espSerial.print(',');
  espSerial.print(pumpOn ? 1 : 0);
  espSerial.print(',');
  espSerial.print(sensorFault ? 1 : 0);
  espSerial.print(',');
  espSerial.println(mistOn ? 1 : 0);
}

// Doc cam bien, quyet dinh tuoi va phun suong, bao cao. Goi moi CONTROL_MS.
void control() {
  // 1. Doc gia tri Analog (0 - 1023), lay trung binh SAMPLE_COUNT lan
  rawValue = readSoilAverage();
  sensorFault = rawValue < SENSOR_FAULT_RAW;

  // 2. Quy doi ra % do am dat (0% - 100%)
  // Luu y: Cam bien dien dung cang kho thi gia tri cang cao, cang uot gia tri cang thap
  moisturePercent = map(rawValue, airValue, waterValue, 0, 100);
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

  // 4. Phun suong theo gio va tinh trang la
  setMist(mistShouldRun(now));

  // 5. In ra Serial Monitor (ai/service.py doc phan "Raw: ... Do am: ...% Bom: ..." de ghi CSV, dung doi)
  Serial.print("Raw: ");
  Serial.print(rawValue);
  Serial.print("  -->  Do am: ");
  Serial.print(moisturePercent);
  Serial.print("%  Bom: ");
  Serial.print(pumpOn ? "ON" : "OFF");
  Serial.print("  Phun: ");
  Serial.print(mistOn ? "ON" : "OFF");
  Serial.print(mistMode == MIST_STOP ? " (dung: la benh)" : mistMode == MIST_BOOST ? " (tang: nhen do)" : "");
  if (watering) {
    Serial.print("  Lan bom: ");
    Serial.print(pulseCount);
    Serial.print('/');
    Serial.print(MAX_PULSES);
  }
  if (secondOfDay(now) < 0) {
    Serial.print("  [CHUA CO GIO: khong phun]");
  }
  if (sensorFault) {
    Serial.print("  [LOI CAM BIEN]");
  }
  if (pumpLocked) {
    Serial.print("  [KHOA BOM: kiem tra nuoc, ong, bom roi bam RESET]");
  }
  Serial.println();

  // 6. Gui do am cho ESP32-CAM moi SOIL_REPORT_MS. SoftwareSerial dang gui thi khong nhan duoc,
  // nen ESP dang gui do 1 dong thi doi nhan xong, khong de dong AI bi mat chu o giua
  if (now - lastSoilReport >= SOIL_REPORT_MS && espLine.len == 0 && espSerial.available() == 0) {
    lastSoilReport = now;
    reportSoil();
  }
}

// Ghi 1 dong LCD: 16 ky tu cua text bat dau tu ky tu thu `from`, thieu thi them dau cach (xoa chu cu)
void showRow(byte row, const char *text, byte from) {
  char buf[17];
  size_t len = strlen(text);
  for (byte i = 0; i < 16; i++) {
    buf[i] = from + i < len ? text[from + i] : ' ';
  }
  buf[16] = '\0';
  if (strcmp(buf, shown[row]) != 0) {
    lcd.setCursor(0, row);
    lcd.print(buf);
    strcpy(shown[row], buf);
  }
}

// Chay chu: dong dai hon 16 ky tu thi hien tu ky tu thu may, t = thoi gian tu luc bat dau hien
byte scrollFrom(const char *text, unsigned long t) {
  int extra = (int)strlen(text) - 16;
  if (extra <= 0) {
    return 0;
  }
  t %= 2 * SCROLL_HOLD_MS + (unsigned long)extra * SCROLL_STEP_MS;
  if (t < SCROLL_HOLD_MS) {
    return 0;
  }
  return min((t - SCROLL_HOLD_MS) / SCROLL_STEP_MS + 1, (unsigned long)extra);
}

void updateLcd(unsigned long now) {
  if (showingAi && now - aiShowStart >= AI_SHOW_MS) {
    showingAi = false;
  }

  if (showingAi) {
    // Man hinh ket qua AI: dong 1 = la, dong 2 = qua (laptop chi co model la thi khong co dong qua)
    unsigned long t = now - aiShowStart;
    const char *fruit = fruitText[0] != '\0' ? fruitText : "Khong ro qua";
    showRow(0, leafText, scrollFrom(leafText, t));
    showRow(1, fruit, scrollFrom(fruit, t));
    return;
  }

  // Man hinh chinh. Dong 1: do am + gio (hoac bao loi cam bien), vd "Do am: 45% 14:05"
  char row[17];
  long sec = secondOfDay(now);
  if (sensorFault) {
    strcpy(row, "LOI CAM BIEN DAT");
  } else if (sec < 0) {
    snprintf(row, sizeof(row), "Do am:%3d%% --:--", moisturePercent);
  } else {
    snprintf(row, sizeof(row), "Do am:%3d%% %02d:%02d", moisturePercent, (int)(sec / 3600), (int)(sec / 60 % 60));
  }
  showRow(0, row, 0);

  // Dong 2: bom + phun suong, vd "Bom:CHO Phun:OFF". CHO = dang trong dot tuoi, doi nuoc ngam; LOI = bom bi khoa
  snprintf(row, sizeof(row), "Bom:%s Phun:%s",
           pumpLocked ? "LOI" : pumpOn ? "ON " : watering ? "CHO" : "OFF", mistOn ? "ON " : "OFF");
  showRow(1, row, 0);
}

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);

  // Tat bom va phun suong truoc roi moi dat OUTPUT, de relay khong bi dong thoang qua luc khoi dong
  digitalWrite(RELAY_PIN, !RELAY_ACTIVE);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(MIST_PIN, !MIST_RELAY_ACTIVE);
  pinMode(MIST_PIN, OUTPUT);
  digitalWrite(MIST_BTN_PIN, LOW);  // INPUT khong pull-up: tha noi, khong bam S1
  pinMode(MIST_BTN_PIN, INPUT);

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
  readSerialPorts();
  if (lastControl == 0 || millis() - lastControl >= CONTROL_MS) {
    lastControl = millis();
    control();
  }
  updateMistButton(millis());
  // lcd.begin() chan ~60 ms: chi lam khi ESP khong dang gui do 1 dong, giong reportSoil()
  if ((pumpOn || mistOn) && millis() - lastLcdReset >= LCD_REFRESH_MS && espLine.len == 0 &&
      espSerial.available() == 0) {
    resetLcdAfterRelay();
  }
  updateLcd(millis());
}
