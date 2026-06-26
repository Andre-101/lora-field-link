/*
  LoRaFieldLink - ESP32-C3 BLE UART bridge for RAK3272-SIP

  Hardware confirmed:
  ESP32-C3 GPIO21 TX -> RAK UART2_RX / PA3
  ESP32-C3 GPIO20 RX <- RAK UART2_TX / PA2
  ESP32-C3 GPIO3      -> RAK RST / PA12
  3.3V and GND shared. Do not power RAK with 5V.

  BLE protocol:
  Nordic UART Service compatible UUIDs.
  Web writes commands to RX characteristic.
  ESP notifies RAK responses on TX characteristic.

  USB serial is kept for PC / Serial USB Terminal debugging.
*/

#include <Arduino.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define RAK_RX_PIN 20
#define RAK_TX_PIN 21
#define RAK_RST_PIN 3
#define RAK_BAUD 115200

#define BLE_DEVICE_NAME "LoRaFieldLink-C3"

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // central writes to ESP
#define NUS_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // ESP notifies central

HardwareSerial RakSerial(1);
BLEServer *bleServer = nullptr;
BLECharacteristic *txCharacteristic = nullptr;
bool bleConnected = false;
bool oldBleConnected = false;

static void bleNotifyChunk(const uint8_t *data, size_t len) {
  if (!bleConnected || txCharacteristic == nullptr || len == 0) return;

  // Safe default for BLE ATT MTU 23 -> 20 payload bytes.
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

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    Serial.println("[BLE] Cliente conectado");
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    Serial.println("[BLE] Cliente desconectado");
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    if (value.length() == 0) return;

    Serial.print("[BLE -> RAK] ");
    Serial.write((const uint8_t *)value.c_str(), value.length());

    RakSerial.write((const uint8_t *)value.c_str(), value.length());
  }
};

static void resetRak() {
  Serial.println("Reiniciando RAK por GPIO3...");
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
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=====================================");
  Serial.println(" LoRaFieldLink ESP32-C3 BLE UART Bridge");
  Serial.println(" USB Serial se mantiene para diagnostico");
  Serial.println(" BLE: LoRaFieldLink-C3");
  Serial.println(" UART RAK: RX GPIO20, TX GPIO21");
  Serial.println(" RAK reset: GPIO3");
  Serial.println("=====================================");

  RakSerial.begin(RAK_BAUD, SERIAL_8N1, RAK_RX_PIN, RAK_TX_PIN);
  resetRak();
  setupBle();

  Serial.println("Listo. Desde USB o BLE envia AT con CR+LF.");
  bleNotifyText("LoRaFieldLink-C3 listo\r\n");
}

void loop() {
  // USB Serial -> RAK, preserves the old workflow.
  while (Serial.available()) {
    RakSerial.write(Serial.read());
  }

  // RAK -> USB Serial and BLE notify.
  static uint8_t buffer[128];
  size_t n = 0;
  while (RakSerial.available() && n < sizeof(buffer)) {
    buffer[n++] = (uint8_t)RakSerial.read();
  }

  if (n > 0) {
    Serial.write(buffer, n);
    bleNotifyChunk(buffer, n);
  }

  // Restart advertising after disconnect.
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
