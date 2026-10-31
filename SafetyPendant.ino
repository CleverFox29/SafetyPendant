#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>


#define BUZZER_PIN 6
#define SDA_PIN 0
#define SCL_PIN 1
#define DURESS_BUTTON 1
#define SAFE_BUTTON 12

#define SERVICE_UUID "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHARACTERISTIC_UUID "19b10001-e8f2-537e-4f6c-d104768a1214"

#define ID_SAFE 3
#define ID_DURESS 4
#define ARMED_TIMEOUT 30000
#define WARN_AT 15000

#define IMPACT_THRESHOLD 2

typedef enum { STATE_IDLE,
               STATE_ARMED,
               STATE_SOS,
               STATE_ENROLL_SAFE,
               STATE_ENROLL_DURESS } DeviceState;

HardwareSerial fpSerial(1);

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;
DeviceState state = STATE_IDLE;
unsigned long armedAt = 0;
bool warningSent = false;

bool pendingSync = false;
unsigned long connectedAt = 0;

void sendBLE(const char* msg) {

  if (deviceConnected) {
    pCharacteristic->setValue(msg);
    pCharacteristic->notify();
  }
  Serial.print("[BLE OUT] ");
  Serial.println(msg);
}

void triggerSOS() {

  state = STATE_SOS;
  sendBLE("E");
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[SOS] SOS triggered! Buzzer on.");
}

void resetTimer() {

  armedAt = millis();
  warningSent = false;
}



class ServerCallbacks : public BLEServerCallbacks {

  void onConnect(BLEServer* s) {
    deviceConnected = true;
    pendingSync = true;
    connectedAt = millis();
    Serial.println("[BLE] App connected. Sync scheduled in 1s.");
  }

  void onDisconnect(BLEServer* s) {
    deviceConnected = false;
    pendingSync = false;
    Serial.println("[BLE] App disconnected. Restarting advertising...");
    pServer->startAdvertising();
  }
};

class CharCallbacks : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic* c) {

    String val = String(c->getValue().c_str());
    Serial.print("[BLE IN] ");
    Serial.println(val);

    if (val == "ACTIVATE" && state == STATE_IDLE) {
      state = STATE_ARMED;
      resetTimer();
      sendBLE("A");
      Serial.println("[STATE] Device ARMED");
    } else if (val == "DEACTIVATE") {
      if (state == STATE_SOS) return;
      state = STATE_IDLE;
      digitalWrite(BUZZER_PIN, LOW);
      sendBLE("D");
      Serial.println("[STATE] Device DISARMED");
    } else if (val == "ENROLL_NORMAL" && state == STATE_IDLE) {
      state = STATE_ENROLL_SAFE;
      Serial.println("Enroll normal finger requested.");
    } else if (val == "ENROLL_DURESS" && state == STATE_IDLE) {
      state = STATE_ENROLL_DURESS;
      Serial.println("Enroll duress finger requested.");
    }
  }
};

void setup() {

  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && millis() - t < 3000)
    ;

  Serial.println("Commands: E=enroll safe, F=enroll duress, A=arm, D=disarm, P=print accel");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);








  BLEDevice::init("SafetyModule");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new CharCallbacks());
  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.print("MAC Address: ");
  Serial.println(BLEDevice::getAddress().toString().c_str());
  Serial.println("[BLE] Advertising. Waiting for app...");
}

void loop() {

  if (pendingSync && millis() - connectedAt > 1000) {
    pendingSync = false;
    if (state == STATE_ARMED) sendBLE("A");
    else sendBLE("D");
    Serial.println("[BLE] Initial state sync sent.");
  }

  if (Serial.available()) {

    char c = toupper(Serial.read());

    if (c == 'A' && state == STATE_IDLE) {
      state = STATE_ARMED;
      resetTimer();
      sendBLE("A");
      Serial.println("[STATE] Armed via Serial.");
      digitalWrite(BUZZER_PIN, HIGH);
      delay(500);
      digitalWrite(BUZZER_PIN, LOW);
    } else if (c == 'D') {
      state = STATE_IDLE;
      digitalWrite(BUZZER_PIN, LOW);
      sendBLE("D");
      Serial.println("[STATE] Disarmed via Serial.");
    }
  }



  if (state == STATE_SOS) {

    if (digitalRead(DURESS_BUTTON)) {
      Serial.println("[DURESS] Duress button pressed in SOS — buzzer silenced.");
      digitalWrite(BUZZER_PIN, LOW);
    }

    delay(10);
    return;
  }

  if (state == STATE_ARMED) {


    if (state == STATE_SOS) return;

    unsigned long elapsed = millis() - armedAt;

    if (!warningSent && elapsed >= WARN_AT) {
      warningSent = true;
      sendBLE("WARN");
      Serial.println("[WARN] 15 seconds left! Place finger or SOS triggers.");
      for (int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(300);
        digitalWrite(BUZZER_PIN, LOW);
        delay(200);
      }
    }

    if (elapsed >= ARMED_TIMEOUT) {
      Serial.println("[TIMEOUT] No finger detected in time.");
      triggerSOS();
      return;
    }

    if (digitalRead(DURESS_BUTTON)) {
      Serial.println("[SAFE] Primary finger. Timer reset.");
      resetTimer();
      digitalWrite(BUZZER_PIN, HIGH);
      delay(600);
      digitalWrite(BUZZER_PIN, LOW);
    } else if (finger.fingerID == ID_DURESS) {
      Serial.println("[DURESS] Duress finger. Silent SOS sent.");
      sendBLE("E");
      state = STATE_SOS;
    }
  }

  delay(10);
}