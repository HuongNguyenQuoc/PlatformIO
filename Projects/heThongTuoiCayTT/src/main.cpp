#include <Arduino.h>
#include <LiquidCrystal.h>

// Khoi tao LCD 1602 theo cac chan:
// LiquidCrystal(RS, Enable, D4, D5, D6, D7)
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

void setup() {
  Serial.begin(9600);
  
  lcd.begin(16, 2);
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("Hello, World!");
  
  lcd.setCursor(0, 1);
  lcd.print("Arduino Nano OK");
}

void loop() {
  // Giu nguyen chu tren man hinh
}