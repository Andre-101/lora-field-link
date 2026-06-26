/*
  LoRaFieldLink - ESP32-C3 BLE UART bridge for RAK3272-SIP + OLED status

  Hardware:
  ESP32-C3 GPIO21 TX -> RAK UART2_RX / PA3
  ESP32-C3 GPIO20 RX <- RAK UART2_TX / PA2
  ESP32-C3 GPIO3      -> RAK RST / PA12
  OLED I2C validated: SDA GPIO5, SCL GPIO6
  3.3V and GND shared. Do not power RAK with 5V.

  Arduino IDE:
  USB CDC On Boot = Enabled
  Serial Monitor = 115200

  Libraries:
  - U8g2 by oliver
  - ESP32 BLE Arduino included with ESP32 core

  BLE protocol:
  Nordic UART Service compatible UUIDs.
  Web writes commands to RX characteristic.
  ESP notifies RAK responses on TX characteristic.

  USB serial is kept for PC / Serial USB Terminal debugging.
*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define RAK_RX_PIN 20
#define RAK_TX_PIN 21
#define RAK_RST_PIN 3
#define RAK_BAUD 115200

#define OLED_SDA_PIN 5
#define OLED_SCL_PIN 6

#define BLE_DEVICE_NAME "LoRaFieldLink-C3"

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // central writes to ESP
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // ESP notifies central

HardwareSerial RakSerial(1);
U8G2_SSD1306_72X40_ER_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

BLEServer *bleServer = nullptr;
BLECharacteristic *txCharacteristic = nullptr;
bool bleConnected = false;
bool oldBleConnected = false;

struct NodeState {
  bool oledOk = false;
  bool rakSeen = false;
  int joinState = -1; // -1 unknown, 0 not joined, 1 joined
  uint32_t txCount = 0;
  char lastPayload[9] = "--";
  char lastResult[17] = "BOOT";
  char lastLine[49] = "";
  uint32_t lastRxMs = 0;
} state;

String rakLineBuffer;
uint32_t lastDisplayMs = 0;

static void drawStatus(const char *phase = nullptr) {
  if (!state.oledOk) return;

  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);

  oled.drawStr(0, 7, "LoRaField");
  oled.drawHLine(0, 9, 72);

  char line[24];
  snprintf(line, sizeof(line), "B:%s R:%s", bleConnected ? "OK" : "--", state.rakSeen ? "OK" : "--");
  oled.drawStr(0, 18, line);

  const char *joinText = "?";
  if (state.joinState == 1) joinText = "YES";
  else if (state.joinState == 0) joinText = "NO";
  snprintf(line, sizeof(line), "J:%s TX:%lu", joinText, (unsigned long)state.txCount);
  oled.drawStr(0, 27, line);

  if (phase && phase[0]) {
    oled.drawStr(0, 38, phase);
  } else {
    snprintf(line, sizeof(line), "P:%s %s", state.lastPayload, state.lastResult);
    oled.drawStr(0, 38, line);
  }

  oled.sendBuffer();
}

static void setResult(const char *text) {
  strncpy(state.lastResult, text, sizeof(state.lastResult) - 1);
  state.lastResult[sizeof(state.lastResult) - 1] = '\0';
  drawStatus();
}

static void parseRakLine(const String &line) {
  String s = line;
  s.trim();
  if (s.length() == 0) return;

  state.rakSeen = true;
  state.lastRxMs = millis();
  strncpy(state.lastLine, s.c_str(), sizeof(state.lastLine) - 1);
  state.lastLine[sizeof(state.lastLine) - 1] = '\0';

  if (s.indexOf("+EVT:JOINED") >= 0 || s.indexOf("AT+NJS=1") >= 0) {
    state.joinState = 1;
    setResult("JOIN OK");
  } else if (s.indexOf("JOIN_FAILED") >= 0 || s.indexOf("AT+NJS=0") >= 0) {
    state.joinState = 0;
    setResult("NO JOIN");
  } else if (s.indexOf("AT_PARAM_ERROR") >= 0 || s.indexOf("ERROR") >= 0) {
    setResult("ERROR");
  } else if (s.indexOf("RX_TIMEOUT") >= 0) {
    setResult("RX TIMEOUT");
  } else if (s == "OK" || s.endsWith(" OK")) {
    setResult("OK");
  }
}

static void parseRakBytes(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '\r' || c == '\n') {
      parseRakLine(rakLineBuffer);
      rakLineBuffer = "";
    } else if (rakLineBuffer.length() < 96) {
      rakLineBuffer += c;
    } else {
      rakLineBuffer = "";
    }
  }
}

static void bleNotifyChunk(const uint8_t *data, size_t len) {
  if (!bleConnected || txCharacteristic == nullptr || len == 0) return;

  const size_t chunkSize = 20;
  for (size_t i = 0; i < len; i += chunkSize) {
    size_t n = min(chunkSize, len - i);
    txCharacteristic->setValue((uint8_t *)(data + i), n);
    txCharacteristic->notify();
    delay(8);
  }
}

static void bleNotifyText(const char *text) {
  bleNotifyChunk((const uint8_t *)text, strlen(text));
}

static void inspectOutgoingCommand(const String &value) {
  String s = value;
  s.trim();
  if (s.startsWith("AT+SEND=")) {
    int colon = s.indexOf(':');
    if (colon >= 0 && colon + 1 < (int)s.length()) {
      String payload = s.substring(colon + 1);
      payload.trim();
      payload.toUpperCase();
      strncpy(state.lastPayload, payload.c_str(), sizeof(state.lastPayload) - 1);
      state.lastPayload[sizeof(state.lastPayload) - 1] = '\0';
    }
    state.txCount++;
    setResult("TX...");
  } else if (s.startsWith("AT+JOIN")) {
    setResult("JOIN...");
  } else if (s.startsWith("AT+NJS")) {
    setResult("CHECK");
  } else if (s == "AT") {
    setResult("AT...");
  }
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    Serial.println("[BLE] Cliente conectado");
    setResult("BLE OK");
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    Serial.println("[BLE] Cliente desconectado");
    setResult("BLE OFF");
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    if (value.length() == 0) return;

    Serial.print("[BLE -> RAK] ");
    Serial.write((const uint8_t *)value.c_str(), value.length());

    inspectOutgoingCommand(value);
    RakSerial.write((const uint8_t *)value.c_str(), value.length());
  }
};

static void setupOled() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  state.oledOk = oled.begin();
  if (state.oledOk) {
    oled.setBusClock(400000);
    oled.setContrast(180);
    drawStatus("BOOT");
    Serial.println("OLED listo: SSD1306 72x40 I2C, SDA GPIO5, SCL GPIO6");
  } else {
    Serial.println("OLED no detectada. El firmware continua sin pantalla.");
  }
}

static void resetRak() {
  Serial.println("Reiniciando RAK por GPIO3...");
  drawStatus("RAK RST");
  pinMode(RAK_RST_PIN, OUTPUT);
  digitalWrite(RAK_RST_PIN, HIGH);
  delay(200);
  digitalWrite(RAK_RST_PIN, LOW);
  delay(200);
  digitalWrite(RAK_RST_PIN, HIGH);
  delay(2500);
}

static void setupBle() {
  BLEDevice::init(BLE_DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  BLEService *service = bleServer->createService(NUS_SERVICE_UUID);

  txCharacteristic = service->createCharacteristic(
    NUS_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  txCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *rxCharacteristic = service->createCharacteristic(
    NUS_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxCharacteristic->setCallbacks(new RxCallbacks());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(NUS_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE listo: LoRaFieldLink-C3");
  setResult("BLE ADV");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=====================================");
  Serial.println(" LoRaFieldLink ESP32-C3 BLE UART Bridge + OLED");
  Serial.println(" USB Serial se mantiene para diagnostico");
  Serial.println(" BLE: LoRaFieldLink-C3");
  Serial.println(" UART RAK: RX GPIO20, TX GPIO21");
  Serial.println(" RAK reset: GPIO3");
  Serial.println(" OLED I2C: SDA GPIO5, SCL GPIO6");
  Serial.println("=====================================");

  setupOled();
  RakSerial.begin(RAK_BAUD, SERIAL_8N1, RAK_RX_PIN, RAK_TX_PIN);
  resetRak();
  setupBle();

  Serial.println("Listo. Desde USB o BLE envia AT con CR+LF.");
  bleNotifyText("LoRaFieldLink-C3 listo\r\n");
  drawStatus();
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    RakSerial.write(c);
  }

  static uint8_t buffer[128];
  size_t n = 0;
  while (RakSerial.available() && n < sizeof(buffer)) {
    buffer[n++] = (uint8_t)RakSerial.read();
  }

  if (n > 0) {
    Serial.write(buffer, n);
    parseRakBytes(buffer, n);
    bleNotifyChunk(buffer, n);
  }

  if (millis() - lastDisplayMs > 1200) {
    lastDisplayMs = millis();
    drawStatus();
  }

  if (!bleConnected && oldBleConnected) {
    delay(500);
    bleServer->startAdvertising();
    Serial.println("[BLE] Advertising reiniciado");
    oldBleConnected = bleConnected;
  }

  if (bleConnected && !oldBleConnected) {
    oldBleConnected = bleConnected;
  }

  delay(2);
}
