// Cau hinh phan cung va mang cua Raspberry Pi Pico 2 W. Doi chan o day thi sua ca diagram.json (so do mach Wokwi).
#pragma once

#include <stdint.h>

// ===== Chan Raspberry Pi Pico 2 W (so GPIO, vd 26 = chan GP26) =====
constexpr int SOIL_PIN = 26;        // Thanh truot thay cam bien do am dat (ADC0)
constexpr int LIGHT_PIN = 27;       // Cam bien anh sang (quang tro), chan AO (ADC1)
constexpr int DHT_PIN = 15;         // DHT22: nhiet do + do am khong khi
constexpr int TANK_TRIG_PIN = 10;   // HC-SR04 do muc nuoc be: chan TRIG...
constexpr int TANK_ECHO_PIN = 11;   // ...va chan ECHO
constexpr int PUMP_PIN = 16;        // LED xanh duong = may bom
constexpr int MIST_PIN = 17;        // LED trang = may phun suong
constexpr int ALARM_PIN = 18;       // LED do = canh bao: khoa bom, het nuoc, loi cam bien dat
constexpr int UNLOCK_BTN_PIN = 14;  // Nut bam (noi xuong GND): mo khoa bom
constexpr int LCD_SDA_PIN = 4;      // LCD 1602 I2C (Wire, I2C0)
constexpr int LCD_SCL_PIN = 5;
constexpr uint8_t LCD_I2C_ADDR = 0x27;

// ===== Wi-Fi: mang gia lap cua Wokwi, co Internet, khong mat khau =====
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// ===== MQTT: broker cong khai, Pico mo phong va may tinh chay AI cung ket noi ra day =====
// Ai biet topic cung doc/ghi duoc, chi dung de demo. Tien to phai trung MQTT_TOPIC_PREFIX trong ai/config.py.
#define MQTT_HOST "broker.hivemq.com"
constexpr uint16_t MQTT_PORT = 1883;
#define MQTT_TOPIC_PREFIX "tuoicaytt/55afb0"

// ===== Gio: lay qua NTP, mui gio Viet Nam UTC+7 =====
#define TZ_VN "ICT-7"
