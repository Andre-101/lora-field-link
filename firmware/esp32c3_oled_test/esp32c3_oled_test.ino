/*
  LoRaFieldLink - ESP32-C3 OLED validation sketch

  Purpose:
  Validate the onboard 0.42 inch OLED before integrating it with the BLE/RAK firmware.

  Expected board pins from our guide:
  SDA = GPIO8
  SCL = GPIO9

  Arduino IDE:
  USB CDC On Boot = Enabled
  Serial Monitor = 115200

  Library required:
  U8g2 by oliver
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

#define OLED_SDA_PIN 8
#define OLED_SCL_PIN 9

// Common ESP32-C3 0.42 inch OLED: SSD1306 72x40 I2C.
// If this does not work, the next likely task is trying another U8g2 constructor.
U8G2_SSD1306_72X40_ER_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

void scanI2C() {
  Serial.println("I2C scan...");
  int count = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C device at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      count++;
    }
  }
  Serial.print("I2C devices found: ");
  Serial.println(count);
}

void drawTest(uint32_t counter) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 7, "LoRaFieldLink");
  oled.drawHLine(0, 9, 72);
  oled.drawStr(0, 18, "OLED OK");
  oled.drawStr(0, 27, "SDA8 SCL9");

  char line[20];
  snprintf(line, sizeof(line), "T:%lu", (unsigned long)counter);
  oled.drawStr(0, 38, line);
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" LoRaFieldLink OLED Test");
  Serial.println(" SDA GPIO8 / SCL GPIO9");
  Serial.println(" U8G2 SSD1306 72x40");
  Serial.println("==============================");

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  scanI2C();

  bool ok = oled.begin();
  Serial.print("oled.begin(): ");
  Serial.println(ok ? "OK" : "FAIL");

  if (ok) {
    oled.setContrast(180);
    drawTest(0);
  }
}

void loop() {
  static uint32_t counter = 0;
  drawTest(counter++);
  Serial.println("OLED test tick");
  delay(1000);
}
