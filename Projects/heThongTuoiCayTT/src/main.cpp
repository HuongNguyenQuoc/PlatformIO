#include <Arduino.h>
#include <LiquidCrystal.h>

// Khoi tao LCD theo cac chan da noi: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

const int SOIL_PIN = A0; // Chan doc cam bien do am dat
const int LED_PIN  = A2; // Chan den LED theo yeu cau

// 2 gia tri tam tinh, sau khi do thuc te ban co the dieu chinh lai
int airValue = 650;   // Gia tri khi de ngoai khong khi kho
int waterValue = 300; // Gia tri khi nhung vao coc nuoc

bool ledState = false;

void setup() {
  Serial.begin(9600);
  
  // Cai dat chan A2 lam OUTPUT cho den LED
  pinMode(LED_PIN, OUTPUT);

  lcd.begin(16, 2);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("He Thong Tuoi Cay");
  lcd.setCursor(0, 1);
  lcd.print("Dang khoi dong..");
  delay(1500);
  lcd.clear();
}

void loop() {
  // Dao trang thai den LED o chan A2 (sang 1s, tat 1s do delay(1000) o cuoi loop)
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState ? HIGH : LOW);

  // 1. Doc gia tri Analog tho (0 - 1023)
  int rawValue = analogRead(SOIL_PIN);

  // 2. Quy doi ra % do am dat (0% - 100%)
  int moisturePercent = map(rawValue, airValue, waterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  // 3. Hien thi len LCD1602
  // Dong 1: Do am dat %
  lcd.setCursor(0, 0);
  lcd.print("Do am dat: ");
  lcd.print(moisturePercent);
  lcd.print("%   ");

  // Dong 2: Gia tri Raw (Analog tho) de tien hieu chuan
  lcd.setCursor(0, 1);
  lcd.print("Raw: ");
  lcd.print(rawValue);
  lcd.print("      ");

  // 4. In ra cong Serial Monitor tren may tinh
  Serial.print("Raw: ");
  Serial.print(rawValue);
  Serial.print("  -->  Do am: ");
  Serial.print(moisturePercent);
  Serial.println("%");

  delay(1000); // Cap nhat moi giay
}