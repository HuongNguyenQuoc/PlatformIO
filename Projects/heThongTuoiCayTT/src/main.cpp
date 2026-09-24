#include <Arduino.h>
#include <LiquidCrystal.h>

// Khoi tao LCD 1602 theo cac chan:
// LiquidCrystal(RS, Enable, D4, D5, D6, D7)
// Khoi tao LCD 1602 theo cac chan da hoat dong tot
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int SOIL_PIN = A0; // Chan doc Analog cua cam bien do am dat

// Gia tri hieu chuan mac dinh (ban se cap nhat sau khi test thuc te)
int airValue = 650;   // Gia tri RAW khi cam bien o ngoai khong khi kho (0%)
int waterValue = 300; // Gia tri RAW khi nhung cam bien vao nuoc (100%)

void setup() {
  Serial.begin(9600);
  
  lcd.begin(16, 2);
  lcd.clear();
  
  // Man hinh chao khoi dong
  lcd.setCursor(0, 0);
  lcd.print("Hello, World!");
  
  lcd.print("He Thong Tuoi");
  lcd.setCursor(0, 1);
  lcd.print("Arduino Nano OK");
  lcd.print("Do Am Dat v1.2");
  delay(1500);
  lcd.clear();
}

void loop() {
  // Giu nguyen chu tren man hinh
  // 1. Doc gia tri Analog tho (0 - 1023)
  int rawValue = analogRead(SOIL_PIN);

  // 2. Quy doi ra % do am dat (0% - 100%)
  // Luu y: Cam bien dien dung v1.2 cang kho thi gia tri cang cao, cang uot gia tri cang thap
  int moisturePercent = map(rawValue, airValue, waterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  // 3. Hien thi truc tiep len LCD1602
  // Dong 1: % Do am dat
  lcd.setCursor(0, 0);
  lcd.print("Do am: ");
  lcd.print(moisturePercent);
  lcd.print("%   ");

  // Dong 2: Gia tri Raw tho de de quan sat hieu chuan
  lcd.setCursor(0, 1);
  lcd.print("Raw: ");
  lcd.print(rawValue);
  lcd.print("      ");

  // 4. In ra Serial Monitor
  Serial.print("Raw: ");
  Serial.print(rawValue);
  Serial.print("  -->  Do am: ");
  Serial.print(moisturePercent);
  Serial.println("%");

  delay(500); // Cap nhat moi 0.5 giay
}