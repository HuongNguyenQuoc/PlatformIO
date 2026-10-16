#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "esp_camera.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Chua co include/secrets.h: copy include/secrets.example.h thanh include/secrets.h roi dien WiFi"
#endif

// Mat khau nap code qua WiFi, lay tu esp32cam/secrets.ini qua build_flags trong platformio.ini
#ifndef OTA_PASSWORD
#error "Chua co OTA_PASSWORD: copy secrets.example.ini thanh secrets.ini roi dat mat khau"
#endif

// Mat khau WiFi rieng TuoiCay-CAM do ESP tu phat, khai bao trong include/secrets.h
#ifndef AP_PASSWORD
#error "Chua co AP_PASSWORD trong include/secrets.h: xem include/secrets.example.h"
#endif
static_assert(sizeof(AP_PASSWORD) - 1 >= 8 && sizeof(AP_PASSWORD) - 1 <= 63,
              "AP_PASSWORD phai dai 8-63 ky tu (WPA2), ngan hon thi ESP khong phat duoc WiFi");

// Chan camera OV2640 tren board AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

const int FLASH_LED_PIN = 4;         // LED flash tren bo (sang manh, chi bat khi chup)
const char *HOSTNAME = "esp32cam";   // Laptop goi bang ten http://esp32cam.local
const char *AP_SSID = "TuoiCay-CAM"; // WiFi rieng ESP tu phat: dien thoai vao thang, goi http://192.168.4.1
const int DISCARD_FRAMES = 2;        // Bo vai khung dau: co the la anh cu trong bo dem hoac chua kip chinh sang
const unsigned long WIFI_CHECK_MS = 1000;              // Kiem tra WiFi nha moi giay
const unsigned long WIFI_RETRY_MS = 10000;             // Mat WiFi nha: 10 giay thu ket noi lai 1 lan
const unsigned long WIFI_SLOW_AFTER_MS = 2UL * 60000;  // Mat qua 2 phut (router hong, mang di noi khac)...
const unsigned long WIFI_SLOW_RETRY_MS = 60000;        // ...thi 1 phut moi thu 1 lan, vi moi lan do tim router song TuoiCay-CAM bi khung
const unsigned long WIFI_RESTART_MS = 30UL * 60000;    // Mat WiFi nha qua 30 phut va khong ai vao TuoiCay-CAM thi khoi dong lai ESP32
const unsigned long WIFI_DHCP_TIMEOUT_MS = 10000;      // Vao duoc diem phat ma 10 giay chua duoc cap IP...
const unsigned long WIFI_BAD_AP_MS = 10UL * 60000;     // ...thi bo qua diem phat do 10 phut (vd bo kich song khong chuyen du lieu)
const int MAX_BAD_APS = 4;

// UART sang Arduino Nano qua module BSS138 (3.3V <-> 5V)
const int NANO_RX_PIN = 14;                      // Nhan tu Nano D9 (kenh LV1-HV1)
const int NANO_TX_PIN = 13;                      // Gui sang Nano D8 (kenh LV2-HV2)
const unsigned long RESULT_RESEND_MS = 60000;    // Gui lai ket qua cho Nano moi 60 giay (phong khi Nano reset)
const unsigned int RESULT_MAX_LEN = 16;          // LCD 16 ky tu

WebServer server(80);
Preferences prefs;  // Bo nho NVS: giu ket qua AI qua cac lan mat dien
unsigned long lastWifiOk = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastWifiRetry = 0;
bool staConnected = false;       // Dang vao duoc WiFi nha chua (de chi in IP 1 lan moi khi vua ket noi)
volatile uint8_t wifiFailReason = 0;  // Ly do lan gan nhat khong vao duoc WiFi nha (ma loi cua ESP-IDF)
volatile unsigned long staAssocTime = 0;  // Luc vao duoc diem phat nhung chua co IP (0 = khong o trang thai nay)

// Router va bo kich song co the phat cung ten WiFi. Diem phat nao vao duoc ma khong cap IP thi ghi lai de tranh
struct BadAp {
  uint8_t bssid[6];
  unsigned long since;  // 0 = o trong
};
BadAp badAps[MAX_BAD_APS] = {};
int nextBadAp = 0;
uint8_t tryingBssid[6] = {};  // Diem phat dang thu vao

String lastResult;               // Ket qua AI gan nhat app gui len, vd "BENH 99%"
unsigned long lastResultSent = 0;
String nanoLine;                 // Dong dang nhan do tu Nano

// Do am Nano gui len moi 5 giay (dong "SOIL:<do am %>,<raw>,<bom 0/1>,<loi cam bien 0/1>,<quat 0/1>")
int soilMoisture = 0;
int soilRaw = 0;
bool soilPump = false;
bool soilFault = false;
bool soilFan = false;
unsigned long soilTime = 0;      // 0 = chua nhan duoc lan nao

bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    // Co PSRAM (board AI-Thinker co san 4MB): chup UXGA 1600x1200, 2 bo dem
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10; // 0-63, cang nho cang net
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_SVGA; // 800x600
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Khoi tao camera loi 0x%x\n", err);
    return false;
  }
  return true;
}

void handleCapture() {
  bool flash = server.arg("flash") == "1";
  if (flash) {
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(150); // Cho camera tu chinh do sang theo den flash
  }
  for (int i = 0; i < DISCARD_FRAMES; i++) {
    camera_fb_t *old = esp_camera_fb_get();
    if (old) {
      esp_camera_fb_return(old);
    }
  }
  camera_fb_t *fb = esp_camera_fb_get();
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!fb) {
    server.send(503, "text/plain", "Chup anh that bai");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.client().write(fb->buf, fb->len);
  Serial.printf("Da gui anh %ux%u, %u byte\n", fb->width, fb->height, fb->len);
  esp_camera_fb_return(fb);
}

void sendResultToNano() {
  Serial2.print("AI:");
  Serial2.println(lastResult);
  lastResultSent = millis();
}

// App gui ket qua AI: POST /result, noi dung dang text, vd "BENH 99%".
// Chi giu ky tu ASCII in duoc (LCD khong co dau tieng Viet), bo dau " va \ de /status van la JSON hop le.
void handleResult() {
  String text = server.arg("plain");
  text.trim();
  String clean;
  for (unsigned int i = 0; i < text.length() && clean.length() < RESULT_MAX_LEN; i++) {
    char c = text[i];
    if (c >= 32 && c < 127 && c != '"' && c != '\\') {
      clean += c;
    }
  }
  lastResult = clean;
  prefs.putString("result", lastResult);
  sendResultToNano();
  Serial.printf("Ket qua AI moi: \"%s\" -> da gui xuong Nano\n", lastResult.c_str());
  server.send(200, "text/plain", "OK");
}

void handleNanoLine(const String &line) {
  if (line.startsWith("SOIL:")) {
    int moisture, raw, pump, fault, fan = 0;
    // Nano chay code cu (chua co quat) chi gui 4 truong: van nhan, coi nhu quat tat
    if (sscanf(line.c_str() + 5, "%d,%d,%d,%d,%d", &moisture, &raw, &pump, &fault, &fan) >= 4) {
      soilMoisture = moisture;
      soilRaw = raw;
      soilPump = pump == 1;
      soilFault = fault == 1;
      soilFan = fan == 1;
      soilTime = millis();
    }
  } else if (line.length() > 0) {
    Serial.printf("[Nano] %s\n", line.c_str());  // Vd "AI OK: BENH 99%" xac nhan Nano da nhan
  }
}

// Lay het ky tu Nano gui sang, gap '\n' thi xu ly ca dong. Khong chan loop().
void readNano() {
  while (Serial2.available() > 0) {
    char c = Serial2.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      handleNanoLine(nanoLine);
      nanoLine = "";
    } else if (nanoLine.length() < 40) {
      nanoLine += c;
    }
  }
}

void handleStatus() {
  char soil[120] = "null";
  if (soilTime > 0) {
    snprintf(soil, sizeof(soil),
             "{\"moisture\":%d,\"raw\":%d,\"pump\":%s,\"fault\":%s,\"fan\":%s,\"age_s\":%lu}",
             soilMoisture, soilRaw, soilPump ? "true" : "false", soilFault ? "true" : "false",
             soilFan ? "true" : "false", (millis() - soilTime) / 1000);
  }
  // ip/rssi/bssid: WiFi nha ("0.0.0.0" khi chua vao duoc; bssid = router hay bo kich song nao).
  // ap_ip/ap_clients: WiFi rieng TuoiCay-CAM
  char json[512];
  snprintf(json, sizeof(json),
           "{\"uptime_s\":%lu,\"wifi_ok\":%s,\"ip\":\"%s\",\"rssi\":%d,\"bssid\":\"%s\",\"ap_ip\":\"%s\","
           "\"ap_clients\":%d,\"psram\":%s,\"free_heap\":%u,\"last_result\":\"%s\",\"soil\":%s}",
           millis() / 1000, staConnected ? "true" : "false", WiFi.localIP().toString().c_str(),
           staConnected ? WiFi.RSSI() : 0, staConnected ? WiFi.BSSIDstr().c_str() : "",
           WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum(),
           psramFound() ? "true" : "false", ESP.getFreeHeap(), lastResult.c_str(), soil);
  server.send(200, "application/json", json);
}

void handleRoot() {
  server.send(200, "text/html",
              "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
              "<title>ESP32-CAM</title><h3>ESP32-CAM - He thong tuoi cay</h3>"
              "<p><a href=/capture>/capture</a> | <a href='/capture?flash=1'>/capture?flash=1</a> | "
              "<a href=/status>/status</a></p><img src=/capture style='max-width:100%'>");
}

const char *wifiFailText(uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
      return "khong thay WiFi: sai ten, WiFi 5 GHz, hoac o xa router";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "sai mat khau";
    default:
      return "loi khac";
  }
}

bool isBadAp(const uint8_t *bssid) {
  for (const BadAp &ap : badAps) {
    if (ap.since > 0 && millis() - ap.since < WIFI_BAD_AP_MS && memcmp(ap.bssid, bssid, 6) == 0) {
      return true;
    }
  }
  return false;
}

void markBadAp(const uint8_t *bssid) {
  memcpy(badAps[nextBadAp].bssid, bssid, 6);
  badAps[nextBadAp].since = millis();
  nextBadAp = (nextBadAp + 1) % MAX_BAD_APS;
}

// Quet cac diem phat ten WIFI_SSID, vao diem manh nhat chua bi danh dau hong (quet mat khoang 2-3 giay)
void beginWifi() {
  int n = WiFi.scanNetworks();
  int best = -1;
  bool skipped = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) != WIFI_SSID) {
      continue;
    }
    if (isBadAp(WiFi.BSSID(i))) {
      skipped = true;
    } else if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) {
      best = i;
    }
  }
  if (best >= 0) {
    memcpy(tryingBssid, WiFi.BSSID(best), 6);
    Serial.printf("Dang ket noi WiFi \"%s\" qua %s (kenh %d, %d dBm)...\n", WIFI_SSID, WiFi.BSSIDstr(best).c_str(),
                  WiFi.channel(best), WiFi.RSSI(best));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WiFi.channel(best), WiFi.BSSID(best));
  } else if (skipped) {
    Serial.printf("Chi thay diem phat \"%s\" khong cap IP (bo kich song?). Thu lai sau\n", WIFI_SSID);
  } else {
    Serial.printf("Khong thay WiFi \"%s\" (sai ten, WiFi 5 GHz, hoac o xa router?). Thu lai sau\n", WIFI_SSID);
  }
  WiFi.scanDelete();
}

// Chay 2 mang cung luc: vao WiFi nha (router cap IP) va tu phat TuoiCay-CAM (IP co dinh 192.168.4.1).
// Web server tra loi tren ca 2 dia chi. ESP chi co 1 radio nen TuoiCay-CAM luon phat cung kenh voi router.
// Khong cho trong setup(): TuoiCay-CAM dung duoc ngay ca khi khong co router, checkWifi() in IP khi vao duoc.
void startWifi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false); // Tat tiet kiem dien WiFi de tra anh nhanh, on dinh hon
  // Tu ket noi lai trong checkWifi(): thu vien tu ket noi lai thi do tim router lien tuc, song TuoiCay-CAM chap chon
  WiFi.setAutoReconnect(false);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { staAssocTime = millis(); }, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { staAssocTime = 0; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    staAssocTime = 0;
    // Bo qua ASSOC_LEAVE: do chinh checkWifi() goi WiFi.disconnect() truoc moi lan thu lai
    if (info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE) {
      wifiFailReason = info.wifi_sta_disconnected.reason;
    }
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.printf("Phat WiFi \"%s\"  ->  http://%s/capture\n", AP_SSID, WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("Khong phat duoc WiFi rieng TuoiCay-CAM");
  }
  beginWifi();
  lastWifiRetry = millis();
}

// Goi moi giay trong loop(): in IP khi vua vao WiFi nha, mat thi tu thu lai
void checkWifi(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!staConnected) {
      staConnected = true;
      Serial.printf("Da ket noi \"%s\". IP: %s  ->  http://%s.local/capture\n", WIFI_SSID,
                    WiFi.localIP().toString().c_str(), HOSTNAME);
    }
    lastWifiOk = now;
    return;
  }
  if (staConnected) {
    staConnected = false;
    Serial.println("Mat WiFi nha, TuoiCay-CAM van chay");
  }
  // Dang co dien thoai vao TuoiCay-CAM: khong do tim router, de song TuoiCay-CAM khong bi ngat
  if (WiFi.softAPgetStationNum() > 0) {
    return;
  }
  // Vao duoc diem phat nhung chua co IP: cho, qua WIFI_DHCP_TIMEOUT_MS thi bo diem phat do, thu diem khac ngay
  unsigned long assoc = staAssocTime;
  if (assoc > 0) {
    if (now - assoc >= WIFI_DHCP_TIMEOUT_MS) {
      Serial.printf("Diem phat %02X:%02X:%02X:%02X:%02X:%02X khong cap IP, bo qua %lu phut\n", tryingBssid[0],
                    tryingBssid[1], tryingBssid[2], tryingBssid[3], tryingBssid[4], tryingBssid[5],
                    WIFI_BAD_AP_MS / 60000);
      markBadAp(tryingBssid);
      WiFi.disconnect();
      lastWifiRetry = now;
      beginWifi();
    }
    return;
  }
  unsigned long lost = now - lastWifiOk;
  if (lost >= WIFI_RESTART_MS) {
    Serial.println("Mat WiFi nha qua lau, khoi dong lai ESP32...");
    ESP.restart();
  }
  if (now - lastWifiRetry >= (lost >= WIFI_SLOW_AFTER_MS ? WIFI_SLOW_RETRY_MS : WIFI_RETRY_MS)) {
    lastWifiRetry = now;
    if (wifiFailReason != 0) {
      Serial.printf("Chua vao duoc WiFi nha (ma %u: %s)\n", wifiFailReason, wifiFailText(wifiFailReason));
      wifiFailReason = 0;
    }
    WiFi.disconnect();
    beginWifi();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32-CAM khoi dong");
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  Serial2.begin(9600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
  prefs.begin("ai", false);
  lastResult = prefs.getString("result", "");  // Ket qua truoc khi mat dien, loop() se gui lai cho Nano

  if (!initCamera()) {
    Serial.println("Kiem tra cap camera, nguon 5V >= 2A. Khoi dong lai sau 5 giay...");
    delay(5000);
    ESP.restart();
  }

  startWifi();
  lastWifiOk = millis();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  // Nap code qua WiFi (OTA), laptop chay: pio run -d esp32cam -t upload
  // mDNS da bat o tren nen tat phan mDNS cua ArduinoOTA
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([]() { Serial.println("Dang nhan code moi qua WiFi..."); });
  ArduinoOTA.onEnd([]() { Serial.println("Nap xong, khoi dong lai"); });
  ArduinoOTA.onError([](ota_error_t err) { Serial.printf("Nap qua WiFi loi %u\n", err); });
  ArduinoOTA.begin();

  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.on("/status", handleStatus);
  server.on("/result", HTTP_POST, handleResult);
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  readNano();

  unsigned long now = millis();
  if (lastResult.length() > 0 && (lastResultSent == 0 || now - lastResultSent >= RESULT_RESEND_MS)) {
    sendResultToNano();
  }

  if (now - lastWifiCheck >= WIFI_CHECK_MS) {
    lastWifiCheck = now;
    checkWifi(now);
  }
  delay(2);
}
