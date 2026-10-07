#include <Arduino.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "esp_camera.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Chua co include/secrets.h: copy include/secrets.example.h thanh include/secrets.h roi dien WiFi"
#endif

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
const int DISCARD_FRAMES = 2;        // Bo vai khung dau: co the la anh cu trong bo dem hoac chua kip chinh sang
const unsigned long WIFI_CHECK_MS = 10000;          // Kiem tra WiFi moi 10 giay
const unsigned long WIFI_RESTART_MS = 5UL * 60000;  // Mat WiFi qua 5 phut thi khoi dong lai ESP32

WebServer server(80);
unsigned long lastWifiOk = 0;
unsigned long lastWifiCheck = 0;

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

void handleStatus() {
  char json[200];
  snprintf(json, sizeof(json),
           "{\"uptime_s\":%lu,\"ip\":\"%s\",\"rssi\":%d,\"psram\":%s,\"free_heap\":%u}",
           millis() / 1000, WiFi.localIP().toString().c_str(), WiFi.RSSI(),
           psramFound() ? "true" : "false", ESP.getFreeHeap());
  server.send(200, "application/json", json);
}

void handleRoot() {
  server.send(200, "text/html",
              "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'>"
              "<title>ESP32-CAM</title><h3>ESP32-CAM - He thong tuoi cay</h3>"
              "<p><a href=/capture>/capture</a> | <a href='/capture?flash=1'>/capture?flash=1</a> | "
              "<a href=/status>/status</a></p><img src=/capture style='max-width:100%'>");
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false); // Tat tiet kiem dien WiFi de tra anh nhanh, on dinh hon
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Dang ket noi WiFi \"%s\"", WIFI_SSID);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Da ket noi. IP: %s  ->  http://%s.local/capture\n",
                  WiFi.localIP().toString().c_str(), HOSTNAME);
  } else {
    Serial.println("Chua ket noi duoc WiFi (sai ten/mat khau, hoac WiFi 5 GHz?). Se tu thu lai.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32-CAM khoi dong");
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!initCamera()) {
    Serial.println("Kiem tra cap camera, nguon 5V >= 2A. Khoi dong lai sau 5 giay...");
    delay(5000);
    ESP.restart();
  }

  connectWifi();
  lastWifiOk = millis();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.on("/status", handleStatus);
  server.begin();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastWifiCheck >= WIFI_CHECK_MS) {
    lastWifiCheck = now;
    if (WiFi.status() == WL_CONNECTED) {
      lastWifiOk = now;
    } else if (now - lastWifiOk >= WIFI_RESTART_MS) {
      Serial.println("Mat WiFi qua lau, khoi dong lai ESP32...");
      ESP.restart();
    } else {
      Serial.println("Mat WiFi, dang ket noi lai...");
      WiFi.reconnect();
    }
  }
  delay(2);
}
