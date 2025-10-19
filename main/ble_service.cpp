#include "ble_service.h"
#include "logger.h"
#include "led.h"
#include "wifi_service.h"

NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* pCharacteristic_wifi = nullptr;
static NimBLECharacteristic* pCharacteristic_pass = nullptr;
static NimBLECharacteristic* pCharacteristic_led = nullptr;

#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID_WIFI "abcd1234-abcd-1234-abcd-1234567890ab"
#define CHARACTERISTIC_UUID_PASS "abcd1234-abcd-1234-abcd-1234567890ac"
#define CHARACTERISTIC_UUID_LED "abcd1234-abcd-1234-abcd-1234567890ae"

#define STAT_UUID "abcd1234-abcd-1234-abcd-1234567890ai"

// Mac address: A0:85:E3:AE:2A:F6

String wifiSsid = "";
String wifiPass = "";

void tryConnect() {
  if (wifiSsid.length() > 0 && wifiPass.length() > 0) {
    initWifi(wifiSsid.c_str(), wifiPass.c_str());
  }
}

class WriteCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    const std::string& uuid = c->getUUID().toString();
    std::string val = c->getValue();

    if (uuid == CHARACTERISTIC_UUID_WIFI) {
      wifiSsid = val.c_str();
      tryConnect();
      LOG_PRINTLN(val.c_str());
    } else if (uuid == CHARACTERISTIC_UUID_PASS) {
      wifiPass = val.c_str();
      tryConnect();
      LOG_PRINTLN(val.c_str());
    } else if (uuid == CHARACTERISTIC_UUID_LED) {
      if (val == "on") {
        led_on(255, 0, 0);
      } else {
        led_off();
      }
    }
  }
};

void initBLE() {
  NimBLEDevice::init("YOUTUBE_Config");
  pServer = NimBLEDevice::createServer();

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic_wifi = pService->createCharacteristic(
    CHARACTERISTIC_UUID_WIFI,
    NIMBLE_PROPERTY::WRITE);

  pCharacteristic_pass = pService->createCharacteristic(
    CHARACTERISTIC_UUID_PASS,
    NIMBLE_PROPERTY::WRITE);

  pCharacteristic_led = pService->createCharacteristic(
    CHARACTERISTIC_UUID_LED,
    NIMBLE_PROPERTY::WRITE);

  // STATUS c notify
  // chStatus = svc->createCharacteristic(STAT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  // chStatus->createDescriptor("Yes I AM :)");

  static WriteCB cb;  // один инстанс колбэков
  pCharacteristic_wifi->setCallbacks(&cb);
  pCharacteristic_pass->setCallbacks(&cb);
  pCharacteristic_led->setCallbacks(&cb);

  pService->start();
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  LOG_PRINTLN("The BLE initialized successfully.");
}