/*
  LoRaFieldLink - ESP32-C3 BLE/USB to RAK3272 UART bridge
  ---------------------------------------------------------

  Purpose
  -------
  This firmware turns an ESP32-C3 OLED board into a field bridge between:

    1. A phone or computer interface.
    2. A RAK3272-SIP LoRaWAN module controlled by AT commands.
    3. The onboard 0.42 inch OLED display used as a local status indicator.

  Communication modes
  -------------------
  - Phone / Chrome Android:
      Web Bluetooth -> ESP32-C3 BLE GATT -> RAK UART.

  - Computer / browser or serial monitor:
      USB CDC Serial -> ESP32-C3 -> RAK UART.

  The RAK3272 remains the LoRaWAN modem. This firmware does not store TTN
  credentials, does not configure AppKey, and does not implement a LoRaWAN stack.
  It only forwards AT commands and mirrors the RAK responses.

  Hardware connections
  --------------------
  ESP32-C3 GPIO21 / TX  -> RAK3272 UART2_RX / PA3
  ESP32-C3 GPIO20 / RX  <- RAK3272 UART2_TX / PA2
  ESP32-C3 GPIO3        -> RAK3272 RST / PA12
  ESP32-C3 3.3V         -> RAK3272 3.3V
  ESP32-C3 GND          -> RAK3272 GND

  Onboard OLED display:
  SDA = GPIO5
  SCL = GPIO6

  Important electrical notes
  --------------------------
  - Power the RAK3272-SIP with 3.3 V only.
  - Keep GND shared between ESP32-C3 and RAK3272.
  - Connect the LoRa antenna before transmitting.
  - Do not connect RAK UART pins to 5 V logic.

  Arduino IDE settings
  --------------------
  Board: select the corresponding ESP32-C3 board profile.
  USB CDC On Boot: Enabled
  Serial Monitor: 115200 baud

  Required libraries
  ------------------
  - U8g2 by oliver
  - ESP32 BLE Arduino, included with the ESP32 Arduino core

  Values commonly modified in future revisions
  --------------------------------------------
  RAK_RX_PIN / RAK_TX_PIN:
    UART pins between ESP32-C3 and RAK3272.

  RAK_RST_PIN:
    GPIO used to reset the RAK3272. Set ENABLE_RAK_RESET to 0 if reset wiring is
    not available.

  OLED_SDA_PIN / OLED_SCL_PIN:
    I2C pins for the onboard display.

  BLE_DEVICE_NAME:
    Name shown by Chrome Android during BLE pairing.

  OLED_REFRESH_MS:
    Minimum time between periodic display refreshes. Event-driven screen updates
    still happen immediately when important state changes occur.

  BLE_CHUNK_SIZE:
    BLE payload chunk size. 20 bytes is safe for the default ATT MTU.
*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// -----------------------------------------------------------------------------
// Hardware configuration
// -----------------------------------------------------------------------------

#define RAK_RX_PIN 20          // ESP32-C3 receives from RAK UART2_TX / PA2
#define RAK_TX_PIN 21          // ESP32-C3 transmits to RAK UART2_RX / PA3
#define RAK_RST_PIN 3          // ESP32-C3 output connected to RAK RST / PA12
#define ENABLE_RAK_RESET 1     // Set to 0 if RAK reset pin is not wired
#define RAK_BAUD 115200        // RAK3272 UART baud rate

#define OLED_SDA_PIN 5         // Onboard OLED I2C data
#define OLED_SCL_PIN 6         // Onboard OLED I2C clock
#define OLED_REFRESH_MS 1000   // Periodic display refresh interval

// -----------------------------------------------------------------------------
// BLE configuration
// -----------------------------------------------------------------------------

#define BLE_DEVICE_NAME "LoRaFieldLink-C3"
#define BLE_CHUNK_SIZE 20

// Nordic UART Service compatible UUIDs.
// RX: phone/browser writes commands to ESP32-C3.
// TX: ESP32-C3 notifies phone/browser with RAK responses.
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// -----------------------------------------------------------------------------
// Global objects
// -----------------------------------------------------------------------------

HardwareSerial RakSerial(1);

// 0.42 inch OLED used by this ESP32-C3 board.
// If a future board uses another display, this constructor is the first value to
// replace. The verified board uses SSD1306 72x40 over I2C.
U8G2_SSD1306_72X40_ER_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

BLEServer *bleServer = nullptr;
BLECharacteristic *bleTxCharacteristic = nullptr;
bool bleConnected = false;
bool previousBleConnected = false;

// -----------------------------------------------------------------------------
// Runtime state shown on the OLED display
// -----------------------------------------------------------------------------

struct NodeState {
  bool oledReady = false;
  bool rakSeen = false;
  int joinState = -1;              // -1 unknown, 0 not joined, 1 joined
  uint32_t txCount = 0;            // Counts AT+SEND commands forwarded to RAK
  char lastPayload[9] = "--";      // Last hex payload detected in AT+SEND
  char lastResult[17] = "BOOT";    // Short status shown on OLED bottom line
  char lastRakLine[65] = "";       // Last complete line received from RAK
  uint32_t lastRakRxMs = 0;        // Timestamp of last RAK response
};

NodeState state;
String rakLineBuffer;
String usbCommandBuffer;
uint32_t lastDisplayRefreshMs = 0;

// -----------------------------------------------------------------------------
// OLED display helpers
// -----------------------------------------------------------------------------

void drawStatus(const char *overrideLine = nullptr) {
  if (!state.oledReady) return;

  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);

  oled.drawStr(0, 7, "LoRaFieldLink");
  oled.drawHLine(0, 9, 72);

  char line[24];
  snprintf(line, sizeof(line), "BLE:%s RAK:%s", bleConnected ? "OK" : "--", state.rakSeen ? "OK" : "--");
  oled.drawStr(0, 18, line);

  const char *joinText = "?";
  if (state.joinState == 1) joinText = "YES";
  if (state.joinState == 0) joinText = "NO";

  snprintf(line, sizeof(line), "JOIN:%s TX:%lu", joinText, (unsigned long)state.txCount);
  oled.drawStr(0, 27, line);

  if (overrideLine != nullptr && overrideLine[0] != '\0') {
    oled.drawStr(0, 38, overrideLine);
  } else {
    snprintf(line, sizeof(line), "P:%s %s", state.lastPayload, state.lastResult);
    oled.drawStr(0, 38, line);
  }

  oled.sendBuffer();
}

void setResult(const char *text) {
  strncpy(state.lastResult, text, sizeof(state.lastResult) - 1);
  state.lastResult[sizeof(state.lastResult) - 1] = '\0';
  drawStatus();
}

void setupOled() {
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  state.oledReady = oled.begin();

  if (state.oledReady) {
    oled.setBusClock(400000);
    oled.setContrast(180);
    drawStatus("BOOT");
    Serial.println("[OLED] Ready: SSD1306 72x40, SDA GPIO5, SCL GPIO6");
  } else {
    Serial.println("[OLED] Not detected. Bridge will continue without display.");
  }
}

// -----------------------------------------------------------------------------
// RAK response parsing
// -----------------------------------------------------------------------------

void parseRakLine(const String &rawLine) {
  String line = rawLine;
  line.trim();
  if (line.length() == 0) return;

  state.rakSeen = true;
  state.lastRakRxMs = millis();
  strncpy(state.lastRakLine, line.c_str(), sizeof(state.lastRakLine) - 1);
  state.lastRakLine[sizeof(state.lastRakLine) - 1] = '\0';

  if (line.indexOf("+EVT:JOINED") >= 0 || line.indexOf("AT+NJS=1") >= 0) {
    state.joinState = 1;
    setResult("JOIN OK");
    return;
  }

  if (line.indexOf("JOIN_FAILED") >= 0 || line.indexOf("AT+NJS=0") >= 0) {
    state.joinState = 0;
    setResult("NO JOIN");
    return;
  }

  if (line.indexOf("RX_TIMEOUT") >= 0) {
    setResult("RX TIMEOUT");
    return;
  }

  if (line.indexOf("AT_PARAM_ERROR") >= 0 || line.indexOf("ERROR") >= 0) {
    setResult("ERROR");
    return;
  }

  if (line == "OK" || line.endsWith(" OK")) {
    setResult("OK");
    return;
  }
}

void parseRakBytes(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];

    if (c == '\r' || c == '\n') {
      parseRakLine(rakLineBuffer);
      rakLineBuffer = "";
    } else if (rakLineBuffer.length() < 96) {
      rakLineBuffer += c;
    } else {
      // Prevent an unbounded line if a malformed stream arrives.
      rakLineBuffer = "";
    }
  }
}

// -----------------------------------------------------------------------------
// Outgoing command inspection
// -----------------------------------------------------------------------------

void inspectOutgoingCommand(const String &rawCommand) {
  String command = rawCommand;
  command.trim();
  if (command.length() == 0) return;

  if (command.startsWith("AT+SEND=")) {
    int colon = command.indexOf(':');

    if (colon >= 0 && colon + 1 < (int)command.length()) {
      String payload = command.substring(colon + 1);
      payload.trim();
      payload.toUpperCase();
      strncpy(state.lastPayload, payload.c_str(), sizeof(state.lastPayload) - 1);
      state.lastPayload[sizeof(state.lastPayload) - 1] = '\0';
    }

    state.txCount++;
    setResult("TX...");
    return;
  }

  if (command.startsWith("AT+JOIN")) {
    setResult("JOIN...");
    return;
  }

  if (command.startsWith("AT+NJS")) {
    setResult("CHECK");
    return;
  }

  if (command == "AT") {
    setResult("AT...");
    return;
  }
}

void inspectUsbByte(char c) {
  if (c == '\r' || c == '\n') {
    inspectOutgoingCommand(usbCommandBuffer);
    usbCommandBuffer = "";
  } else if (usbCommandBuffer.length() < 96) {
    usbCommandBuffer += c;
  } else {
    usbCommandBuffer = "";
  }
}

// -----------------------------------------------------------------------------
// BLE transport
// -----------------------------------------------------------------------------

void bleNotifyChunk(const uint8_t *data, size_t len) {
  if (!bleConnected || bleTxCharacteristic == nullptr || len == 0) return;

  for (size_t i = 0; i < len; i += BLE_CHUNK_SIZE) {
    size_t chunkLen = min((size_t)BLE_CHUNK_SIZE, len - i);
    bleTxCharacteristic->setValue((uint8_t *)(data + i), chunkLen);
    bleTxCharacteristic->notify();
    delay(8);
  }
}

void bleNotifyText(const char *text) {
  bleNotifyChunk((const uint8_t *)text, strlen(text));
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    Serial.println("[BLE] Client connected");
    setResult("BLE OK");
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected");
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

void setupBle() {
  BLEDevice::init(BLE_DEVICE_NAME);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  BLEService *service = bleServer->createService(NUS_SERVICE_UUID);

  bleTxCharacteristic = service->createCharacteristic(
    NUS_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  bleTxCharacteristic->addDescriptor(new BLE2902());

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

  Serial.print("[BLE] Advertising as ");
  Serial.println(BLE_DEVICE_NAME);
  setResult("BLE ADV");
}

// -----------------------------------------------------------------------------
// RAK reset
// -----------------------------------------------------------------------------

void resetRak() {
#if ENABLE_RAK_RESET
  Serial.println("[RAK] Reset pulse on GPIO3");
  drawStatus("RAK RST");

  pinMode(RAK_RST_PIN, OUTPUT);
  digitalWrite(RAK_RST_PIN, HIGH);
  delay(200);
  digitalWrite(RAK_RST_PIN, LOW);
  delay(200);
  digitalWrite(RAK_RST_PIN, HIGH);
  delay(2500);
#else
  Serial.println("[RAK] Reset disabled by configuration");
#endif
}

// -----------------------------------------------------------------------------
// Arduino lifecycle
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("================================================");
  Serial.println(" LoRaFieldLink ESP32-C3 BLE/USB UART Bridge");
  Serial.println(" BLE device: " BLE_DEVICE_NAME);
  Serial.println(" RAK UART: RX GPIO20, TX GPIO21, 115200 8N1");
  Serial.println(" RAK reset: GPIO3");
  Serial.println(" OLED: SSD1306 72x40, SDA GPIO5, SCL GPIO6");
  Serial.println("================================================");

  setupOled();
  RakSerial.begin(RAK_BAUD, SERIAL_8N1, RAK_RX_PIN, RAK_TX_PIN);
  resetRak();
  setupBle();

  Serial.println("[READY] Send AT commands from USB Serial or BLE.");
  bleNotifyText("LoRaFieldLink-C3 ready\r\n");
  drawStatus();
}

void loop() {
  // USB CDC Serial -> RAK UART.
  // This path is intended for PC/lab use and keeps the bridge useful even when
  // the phone interface is not connected.
  while (Serial.available()) {
    char c = (char)Serial.read();
    inspectUsbByte(c);
    RakSerial.write(c);
  }

  // RAK UART -> USB CDC Serial and BLE notification.
  // Data is batched to reduce overhead while keeping responses responsive.
  static uint8_t buffer[128];
  size_t len = 0;

  while (RakSerial.available() && len < sizeof(buffer)) {
    buffer[len++] = (uint8_t)RakSerial.read();
  }

  if (len > 0) {
    Serial.write(buffer, len);
    parseRakBytes(buffer, len);
    bleNotifyChunk(buffer, len);
  }

  // Periodic display refresh. Most updates are event-driven, but this keeps the
  // screen consistent after connection transitions or missed redraws.
  if (millis() - lastDisplayRefreshMs >= OLED_REFRESH_MS) {
    lastDisplayRefreshMs = millis();
    drawStatus();
  }

  // After a BLE disconnection, advertising must be started again so the phone can
  // reconnect without rebooting the ESP32-C3.
  if (!bleConnected && previousBleConnected) {
    delay(500);
    bleServer->startAdvertising();
    Serial.println("[BLE] Advertising restarted");
    previousBleConnected = bleConnected;
  }

  if (bleConnected && !previousBleConnected) {
    previousBleConnected = bleConnected;
  }

  delay(2);
}
