// He thong tuoi cay thong minh co AI - firmware Raspberry Pi Pico 2 W, chay tren trinh mo phong Wokwi.
//
// Doc cam bien (do am dat, nhiet do / do am khong khi, anh sang, muc nuoc be), tu tuoi va phun suong,
// hien len LCD, gui so lieu qua MQTT cho may tinh chay AI (ai/service.py) va nhan lai ket qua AI / lenh.
// Logic quyet dinh nam trong lib/FarmCore; file nay lo phan cung, mang va hien thi.
//
// Lenh (go vao Serial Monitor, hoac gui toi topic <MQTT_TOPIC_PREFIX>/cmd):
//   AI:<dong 1>|<dong 2>   ket qua AI moi, LCD hien AI_SHOW_MS. "AI:" de trong = thoi hien. Tra loi "AI OK"
//   MIST:<1-3>             che do phun suong: 1 binh thuong, 2 dung (la benh), 3 phun tang (nhen do)
//   TIME:hh:mm:ss          chinh gio bang tay de thu nhanh (NTP se chinh lai sau)
//   UNLOCK                 mo khoa bom, giong bam nut
//
// MQTT topic (tien to MQTT_TOPIC_PREFIX trong include/config.h):
//   .../telemetry   Pico gui JSON so lieu cam bien + trang thai moi TELEMETRY_MS
//   .../cmd         may tinh / web / app gui lenh (nhu tren)
//   .../ack         Pico tra loi tung lenh
//   .../status      "online" / "offline" (giu lai tren broker, "offline" do broker tu gui khi Pico mat ket noi)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

#include "FarmCore.h"
#include "config.h"

using namespace farm;

// ===================== Thong so =====================

// Thanh truot do am dat (ADC 12 bit, 0-4095): vi tri thanh truot ~ do am %.
// Keo ve trai cung (duoi ~5%) = gia lap tuot day cam bien.
const int SOIL_FAULT_RAW = 200;
const int SOIL_DRY_RAW = 400;   // 0%
const int SOIL_WET_RAW = 4000;  // 100%
const int SOIL_SAMPLES = 10;    // Doc nhieu lan lay trung binh

// Thoi gian da rut ngan cho mo phong de xem nhanh (vuon that nen de soakMs ~60 s, restMs ~30 phut)
const IrrigationConfig IRRIGATION = {
    50,     // moistureLow: duoi 50% thi bat dau tuoi
    65,     // moistureHigh: len 65% thi dung. Cach 15% de moi dot tuoi dam roi de dat kho bot
    10000,  // dryConfirmMs: kho lien tuc 10 giay moi tuoi
    6000,   // pumpOnMs: moi lan bom 6 giay (~100 ml voi bom 1 lit/phut)
    5000,   // soakMs: cho nuoc ngam 5 giay giua 2 lan bom
    8,      // maxPulses: toi da 8 lan bom moi dot
    60000,  // restMs: chua du am thi nghi 60 giay
    5,      // minRise: bom du 8 lan ma do am tang duoi 5% thi khoa bom
    3,      // maxIncompleteCycles: 3 dot lien tiep chua du am thi khoa bom
};

const MistConfig MIST = {
    30,          // onSec: moi lan phun 30 giay...
    30L * 60,    // everySec: ...30 phut 1 lan (dung :00 va :30)
    15L * 60,    // boostEverySec: ...hoac 15 phut 1 lan khi nhen do
    8L * 3600,   // startSec: chi phun tu 8h...
    16L * 3600,  // endSec: ...den truoc 16h, la kip kho truoc toi (la uot qua dem de benh nam)
    85.0f,       // maxAirHumidity: khong khi da am tu 85% thi khong phun
};

// Be nuoc: HC-SR04 gan tren mieng be, do khoang cach xuong mat nuoc. 100 cm = can, 10 cm = day
const float TANK_EMPTY_CM = 100;
const float TANK_FULL_CM = 10;
const int TANK_LOW_PERCENT = 10;  // Duoi 10% thi coi la het nuoc: khong bom, khong phun...
const int TANK_OK_PERCENT = 15;   // ...len lai 15% moi chay tiep, tranh bat tat lien tuc quanh nguong

// Module quang tro cua Wokwi: dien tro noi tiep 10 kOhm, LDR 50 kOhm o 10 lux, gamma 0.7
const float LDR_SERIES_OHM = 10000;
const float LDR_RL10_KOHM = 50;
const float LDR_GAMMA = 0.7f;

const unsigned long CONTROL_MS = 500;     // Doc do am, anh sang, quyet dinh tuoi / phun
const unsigned long DHT_MS = 2000;        // DHT22 chi doc duoc 1 lan / 2 giay
const unsigned long TANK_MS = 1000;
const unsigned long LOG_MS = 2000;        // In 1 dong trang thai ra Serial
const unsigned long TELEMETRY_MS = 5000;  // Gui so lieu qua MQTT
const unsigned long WIFI_RETRY_MS = 10000;
const unsigned long MQTT_RETRY_MS = 5000;
const unsigned long AI_SHOW_MS = 20000;   // LCD hien ket qua AI 20 giay roi ve man hinh chinh
const unsigned long PAGE_MS = 4000;       // Man hinh chinh co 2 trang, doi moi 4 giay
const unsigned long SCROLL_HOLD_MS = 1500;
const unsigned long SCROLL_STEP_MS = 350;
const unsigned long DEBOUNCE_MS = 50;
const size_t AI_TEXT_LEN = 40;            // Moi dong ket qua AI toi da 40 ky tu (dai hon 16 thi chay chu)
const time_t MIN_VALID_TIME = 1700000000; // Truoc 11/2023: dong ho chua duoc chinh

const char *TOPIC_TELEMETRY = MQTT_TOPIC_PREFIX "/telemetry";
const char *TOPIC_CMD = MQTT_TOPIC_PREFIX "/cmd";
const char *TOPIC_ACK = MQTT_TOPIC_PREFIX "/ack";
const char *TOPIC_STATUS = MQTT_TOPIC_PREFIX "/status";

// ===================== Trang thai =====================

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);
DHT dht(DHT_PIN, DHT22);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Irrigation irrigation(IRRIGATION);

struct Sensors {
  int soilRaw = 0;
  int moisture = 0;
  bool soilFault = false;
  float temperature = NAN;
  float airHumidity = NAN;
  bool dhtOk = false;
  int lightRaw = 0;
  float lux = 0;
  float tankCm = NAN;
  int tankLevel = -1;  // -1 = chua do duoc
  bool tankEmpty = false;
} sensors;

int mistMode = MIST_NORMAL;
MistDecision mist = {false, MistReason::NoClock};

char aiLine1[AI_TEXT_LEN + 1] = "";
char aiLine2[AI_TEXT_LEN + 1] = "";
bool showingAi = false;
unsigned long aiShowStart = 0;

char shown[2][17] = {"", ""};  // Noi dung dang hien tren LCD: chi ghi lai dong nao doi

unsigned long lastControl = 0, lastDht = 0, lastTank = 0, lastLog = 0, lastTelemetry = 0;
unsigned long lastWifiTry = 0, lastMqttTry = 0;
bool wifiReported = false;
char mqttClientId[24];
String serialLine;
bool buttonDown = false;
unsigned long buttonChange = 0;

// ===================== Gio =====================

// Giay trong ngay (0-86399) theo gio Viet Nam, -1 neu chua biet gio
long secondOfDay() {
  time_t now = time(nullptr);
  if (now < MIN_VALID_TIME) {
    return -1;
  }
  struct tm t;
  localtime_r(&now, &t);
  return t.tm_hour * 3600L + t.tm_min * 60L + t.tm_sec;
}

// Chinh gio trong ngay bang tay (giu nguyen ngay; chua co ngay thi lay 1/1/2026)
void setClock(int h, int m, int s) {
  time_t now = time(nullptr);
  struct tm t = {};
  if (now >= MIN_VALID_TIME) {
    localtime_r(&now, &t);
  } else {
    t.tm_year = 2026 - 1900;
    t.tm_mday = 1;
  }
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = s;
  t.tm_isdst = 0;
  struct timeval tv = {mktime(&t), 0};
  settimeofday(&tv, nullptr);
}

// "hh:mm:ss" vao buf (it nhat 9 byte), tra ve false neu chua biet gio
bool clockText(char *buf, size_t size) {
  long sec = secondOfDay();
  if (sec < 0) {
    return false;
  }
  snprintf(buf, size, "%02ld:%02ld:%02ld", sec / 3600, sec / 60 % 60, sec % 60);
  return true;
}

// ===================== Cam bien =====================

void readSoil() {
  long sum = 0;
  for (int i = 0; i < SOIL_SAMPLES; i++) {
    sum += analogRead(SOIL_PIN);
  }
  sensors.soilRaw = sum / SOIL_SAMPLES;
  sensors.soilFault = sensors.soilRaw < SOIL_FAULT_RAW;
  sensors.moisture = percentFromRaw(sensors.soilRaw, SOIL_DRY_RAW, SOIL_WET_RAW);
}

void readLight() {
  sensors.lightRaw = analogRead(LIGHT_PIN);
  sensors.lux = luxFromAdc(sensors.lightRaw, 4095, 3.3f, LDR_SERIES_OHM, LDR_RL10_KOHM, LDR_GAMMA);
}

void readDht() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  sensors.dhtOk = !isnan(t) && !isnan(h);
  if (sensors.dhtOk) {
    sensors.temperature = t;
    sensors.airHumidity = h;
  }
}

void readTank() {
  digitalWrite(TANK_TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TANK_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TANK_TRIG_PIN, LOW);
  unsigned long us = pulseIn(TANK_ECHO_PIN, HIGH, 30000);  // Cho toi da 30 ms (~5 m)
  if (us == 0) {
    return;  // Khong co tieng vong: giu so do cu
  }
  sensors.tankCm = us / 58.0f;
  sensors.tankLevel = tankLevelPercent(sensors.tankCm, TANK_EMPTY_CM, TANK_FULL_CM);
  if (sensors.tankLevel < TANK_LOW_PERCENT) {
    sensors.tankEmpty = true;
  } else if (sensors.tankLevel >= TANK_OK_PERCENT) {
    sensors.tankEmpty = false;
  }
}

// ===================== Dieu khien =====================

void control(unsigned long now) {
  readSoil();
  readLight();

  bool pump = irrigation.update(now, sensors.moisture, sensors.soilFault, sensors.tankEmpty);
  digitalWrite(PUMP_PIN, pump ? HIGH : LOW);

  mist = decideMist(MIST, secondOfDay(), mistMode, sensors.airHumidity, sensors.dhtOk, sensors.tankEmpty);
  digitalWrite(MIST_PIN, mist.on ? HIGH : LOW);

  bool alarm = irrigation.locked() || sensors.tankEmpty || sensors.soilFault;
  digitalWrite(ALARM_PIN, alarm ? HIGH : LOW);
}

// Nut mo khoa bom (INPUT_PULLUP: bam = LOW)
void checkButton(unsigned long now) {
  bool down = digitalRead(UNLOCK_BTN_PIN) == LOW;
  if (down != buttonDown && now - buttonChange >= DEBOUNCE_MS) {
    buttonDown = down;
    buttonChange = now;
    if (down && irrigation.locked()) {
      irrigation.unlock();
      Serial.println("Da mo khoa bom (nut bam)");
    }
  }
}

// ===================== Lenh =====================

void copyText(char *dst, const char *src, size_t len) {
  len = len < AI_TEXT_LEN ? len : AI_TEXT_LEN;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

// Xu ly 1 dong lenh tu Serial hoac MQTT, tra ve cau tra loi
String handleCommand(const char *line) {
  if (strncmp(line, "AI:", 3) == 0) {
    const char *text = line + 3;
    const char *bar = strchr(text, '|');
    copyText(aiLine1, text, bar ? (size_t)(bar - text) : strlen(text));
    copyText(aiLine2, bar ? bar + 1 : "", bar ? strlen(bar + 1) : 0);
    showingAi = text[0] != '\0';
    aiShowStart = millis();
    return "AI OK";
  }
  if (strncmp(line, "MIST:", 5) == 0) {
    int mode = atoi(line + 5);
    if (mode >= MIST_NORMAL && mode <= MIST_BOOST) {
      mistMode = mode;
    }
    return String("MIST OK ") + mistMode;
  }
  if (strncmp(line, "TIME:", 5) == 0) {
    int h, m, s;
    if (sscanf(line + 5, "%d:%d:%d", &h, &m, &s) == 3 && h >= 0 && h < 24 && m >= 0 && m < 60 && s >= 0 && s < 60) {
      setClock(h, m, s);
      return "TIME OK";
    }
    return "TIME loi: dung dang TIME:hh:mm:ss";
  }
  if (strcmp(line, "UNLOCK") == 0) {
    irrigation.unlock();
    return "UNLOCK OK";
  }
  return String("Lenh khong hop le: ") + line;
}

// Doc lenh go tren Serial. Ket thuc dong bang '\r' hoac '\n' (terminal Wokwi bam Enter gui '\r')
void readSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (serialLine.length() > 0) {
        Serial.println(handleCommand(serialLine.c_str()));
        serialLine = "";
      }
    } else if (serialLine.length() < 100) {
      serialLine += c;
    }
  }
}

void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  char line[100];
  length = length < sizeof(line) - 1 ? length : sizeof(line) - 1;
  memcpy(line, payload, length);  // Chep ra truoc: publish() ben duoi dung lai bo dem cua payload
  line[length] = '\0';
  while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
    line[--length] = '\0';
  }
  String reply = handleCommand(line);
  Serial.printf("[MQTT] %s -> %s\n", line, reply.c_str());
  mqtt.publish(TOPIC_ACK, reply.c_str());
}

// ===================== Mang =====================

void connectWifi() {
  Serial.printf("Dang ket noi Wi-Fi \"%s\"...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  if (WIFI_PASSWORD[0] == '\0') {
    WiFi.begin(WIFI_SSID);  // Mang mo, khong mat khau (Wokwi-GUEST)
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  lastWifiTry = millis();
}

void publishTelemetry() {
  if (!mqtt.connected()) {
    return;
  }
  JsonDocument doc;
  doc["moisture"] = sensors.moisture;
  doc["soil_raw"] = sensors.soilRaw;
  doc["soil_fault"] = sensors.soilFault;
  if (sensors.dhtOk) {
    doc["temperature"] = roundf(sensors.temperature * 10) / 10;
    doc["air_humidity"] = roundf(sensors.airHumidity * 10) / 10;
  } else {
    doc["temperature"] = nullptr;
    doc["air_humidity"] = nullptr;
  }
  doc["lux"] = (long)sensors.lux;
  if (sensors.tankLevel >= 0) {
    doc["tank_level"] = sensors.tankLevel;
  } else {
    doc["tank_level"] = nullptr;
  }
  doc["tank_empty"] = sensors.tankEmpty;
  doc["pump"] = irrigation.pumpOn();
  doc["pump_state"] = pumpStateName(irrigation.state());
  doc["pulses"] = irrigation.pulseCount();
  doc["mist"] = mist.on;
  doc["mist_mode"] = mistMode;
  doc["mist_reason"] = mistReasonName(mist.reason);
  char clock[9];
  if (clockText(clock, sizeof(clock))) {
    doc["time"] = clock;
  } else {
    doc["time"] = nullptr;
  }
  doc["uptime_s"] = millis() / 1000;

  char buf[512];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(TOPIC_TELEMETRY, (const uint8_t *)buf, n, false);
}

// Goi lien tuc trong loop(): giu Wi-Fi va MQTT, mat thi tu ket noi lai
void checkNetwork(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiReported) {
      wifiReported = false;
      Serial.println("Mat Wi-Fi, se thu lai");
    }
    if (now - lastWifiTry >= WIFI_RETRY_MS) {
      WiFi.disconnect();
      connectWifi();
    }
    return;
  }
  if (!wifiReported) {
    wifiReported = true;
    Serial.printf("Da ket noi Wi-Fi, IP %s. Dang lay gio qua NTP...\n", WiFi.localIP().toString().c_str());
    NTP.begin("pool.ntp.org", "time.google.com");  // Chay nen, mui gio da dat bang TZ trong setup()
  }
  if (!mqtt.connected() && (lastMqttTry == 0 || now - lastMqttTry >= MQTT_RETRY_MS)) {
    lastMqttTry = now;
    Serial.printf("Dang ket noi MQTT %s:%u...\n", MQTT_HOST, MQTT_PORT);
    // Di chuc (last will): Pico mat ket noi thi broker tu dat status = "offline"
    if (mqtt.connect(mqttClientId, nullptr, nullptr, TOPIC_STATUS, 1, true, "offline")) {
      mqtt.publish(TOPIC_STATUS, "online", true);
      mqtt.subscribe(TOPIC_CMD, 1);
      Serial.printf("Da ket noi MQTT. Topic: %s/{telemetry,cmd,ack,status}\n", MQTT_TOPIC_PREFIX);
      publishTelemetry();
    } else {
      Serial.printf("Ket noi MQTT loi (ma %d), thu lai sau %lu giay\n", mqtt.state(), MQTT_RETRY_MS / 1000);
    }
  }
}

// ===================== Hien thi =====================

// Ghi 1 dong LCD: 16 ky tu cua text bat dau tu ky tu thu `from`, thieu thi them dau cach (xoa chu cu)
void showRow(int row, const char *text, size_t from) {
  char buf[17];
  size_t len = strlen(text);
  for (size_t i = 0; i < 16; i++) {
    buf[i] = from + i < len ? text[from + i] : ' ';
  }
  buf[16] = '\0';
  if (strcmp(buf, shown[row]) != 0) {
    lcd.setCursor(0, row);
    lcd.print(buf);
    strcpy(shown[row], buf);
  }
}

// Chay chu: dong dai hon 16 ky tu thi dung o dau SCROLL_HOLD_MS, moi SCROLL_STEP_MS dich 1 ky tu, dung o cuoi
size_t scrollFrom(const char *text, unsigned long t) {
  int extra = (int)strlen(text) - 16;
  if (extra <= 0) {
    return 0;
  }
  t %= 2 * SCROLL_HOLD_MS + (unsigned long)extra * SCROLL_STEP_MS;
  if (t < SCROLL_HOLD_MS) {
    return 0;
  }
  unsigned long step = (t - SCROLL_HOLD_MS) / SCROLL_STEP_MS + 1;
  return step < (unsigned long)extra ? step : extra;
}

void updateLcd(unsigned long now) {
  if (showingAi && now - aiShowStart >= AI_SHOW_MS) {
    showingAi = false;
  }
  if (showingAi) {
    unsigned long t = now - aiShowStart;
    showRow(0, aiLine1, scrollFrom(aiLine1, t));
    showRow(1, aiLine2, scrollFrom(aiLine2, t));
    return;
  }

  char row[32];  // Rong hon 16 cho an toan khi so lieu bat thuong; showRow() chi hien 16 ky tu dau
  if ((now / PAGE_MS) % 2 == 0) {
    // Trang 1: "Dat: 45%   14:05" / "Bom:CHO Phun:OFF"
    long sec = secondOfDay();
    if (sensors.soilFault) {
      strcpy(row, "LOI CAM BIEN DAT");
    } else if (sec < 0) {
      snprintf(row, sizeof(row), "Dat:%3d%%   --:--", sensors.moisture);
    } else {
      snprintf(row, sizeof(row), "Dat:%3d%%   %02ld:%02ld", sensors.moisture, sec / 3600, sec / 60 % 60);
    }
    showRow(0, row, 0);
    snprintf(row, sizeof(row), "Bom:%s Phun:%s", pumpStateCode(irrigation.state()), mist.on ? "ON " : "OFF");
    showRow(1, row, 0);
  } else {
    // Trang 2: "T:28.5C  H: 65%" / "Nuoc: 77% 1200lx"
    if (sensors.dhtOk) {
      snprintf(row, sizeof(row), "T:%4.1fC  H:%3d%%", sensors.temperature, (int)(sensors.airHumidity + 0.5f));
    } else {
      strcpy(row, "T: --.-C H: --%");
    }
    showRow(0, row, 0);
    long lux = sensors.lux > 99999 ? 99999 : (long)sensors.lux;
    if (sensors.tankLevel >= 0) {
      snprintf(row, sizeof(row), "Nuoc:%3d%%%5ldlx", sensors.tankLevel, lux);
    } else {
      snprintf(row, sizeof(row), "Nuoc: --%%%5ldlx", lux);
    }
    showRow(1, row, 0);
  }
}

// 1 dong trang thai ra Serial moi LOG_MS
void printLog() {
  char clock[9] = "--:--:--";
  clockText(clock, sizeof(clock));
  Serial.printf("[%s] Dat %3d%% (raw %4d)%s  KK %.1fC %.0f%%  As %ld lx  Be %d%%  Bom %s",
                clock, sensors.moisture, sensors.soilRaw, sensors.soilFault ? " [LOI CAM BIEN]" : "",
                sensors.temperature, sensors.airHumidity, (long)sensors.lux, sensors.tankLevel,
                pumpStateName(irrigation.state()));
  if (irrigation.watering()) {
    Serial.printf(" (lan %d/%d)", irrigation.pulseCount(), IRRIGATION.maxPulses);
  }
  Serial.printf("  Phun %s (%s, che do %d)  %s\n", mist.on ? "ON" : "OFF", mistReasonName(mist.reason), mistMode,
                mqtt.connected() ? "MQTT ok" : "MQTT chua ket noi");
}

// ===================== setup / loop =====================

void setup() {
  // Serial cua Pico la cong USB: doi toi da 2 giay cho may tinh / Wokwi mo cong, khong mat cac dong in dau tien
  Serial.begin(115200);
  unsigned long serialWait = millis();
  while (!Serial && millis() - serialWait < 2000) {
    delay(10);
  }
  Serial.println("\nHe thong tuoi cay thong minh - Raspberry Pi Pico 2 W (Wokwi)");

  pinMode(PUMP_PIN, OUTPUT);
  pinMode(MIST_PIN, OUTPUT);
  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  digitalWrite(MIST_PIN, LOW);
  digitalWrite(ALARM_PIN, LOW);
  pinMode(UNLOCK_BTN_PIN, INPUT_PULLUP);
  pinMode(TANK_TRIG_PIN, OUTPUT);
  pinMode(TANK_ECHO_PIN, INPUT);
  analogReadResolution(12);
  dht.begin();

  Wire.setSDA(LCD_SDA_PIN);
  Wire.setSCL(LCD_SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Tuoi cay thong");
  lcd.setCursor(0, 1);
  lcd.print("minh + AI  v3.0");
  delay(1500);
  lcd.clear();

  // Dat mui gio ngay tu dau: gio chinh tay (lenh TIME) cung can mui gio
  setenv("TZ", TZ_VN, 1);
  tzset();

  snprintf(mqttClientId, sizeof(mqttClientId), "tuoicaytt-%06lx", (unsigned long)(rp2040.hwrand32() & 0xFFFFFF));
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(512);
  connectWifi();

  Serial.println("Lenh: AI:<dong 1>|<dong 2>   MIST:<1-3>   TIME:hh:mm:ss   UNLOCK");
}

void loop() {
  unsigned long now = millis();
  readSerial();
  checkNetwork(now);
  mqtt.loop();
  checkButton(now);

  if (now - lastDht >= DHT_MS || lastDht == 0) {
    lastDht = now;
    readDht();
  }
  if (now - lastTank >= TANK_MS || lastTank == 0) {
    lastTank = now;
    readTank();
  }
  if (now - lastControl >= CONTROL_MS || lastControl == 0) {
    lastControl = now;
    control(now);
  }
  if (now - lastTelemetry >= TELEMETRY_MS) {
    lastTelemetry = now;
    publishTelemetry();
  }
  if (now - lastLog >= LOG_MS) {
    lastLog = now;
    printLog();
  }
  updateLcd(now);
  delay(5);
}
