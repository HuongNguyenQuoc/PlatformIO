#include <Arduino.h>
#include <LiquidCrystal.h>

// Khoi tao LCD theo cac chan: RS=12, E=11, D4=5, D5=4, D6=3, D7=2
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

void setup() {
  lcd.begin(16, 2);
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("Hello, World!");
  
  lcd.setCursor(0, 1);
  lcd.print("Arduino Nano OK");
}

void loop() {
  // Chuong trinh test giu nguyen chu tren man hinh
}