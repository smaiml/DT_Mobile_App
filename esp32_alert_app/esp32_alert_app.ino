#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- PIN DEFINITIONS ---
#define BUTTON_PIN 25    // Push Button
#define VIBRATION_PIN 26 // Vibration Motor IN
#define BOOT_BUTTON_PIN 0 // Built-in BOOT button

// --- MPU6050 I2C Address and Registers (NO LIBRARY NEEDED) ---
#define MPU6050_ADDR       0x68
#define MPU6050_WHO_AM_I   0x75
#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_ACCEL_XOUT 0x3B  // Start of 14 bytes: accel(6) + temp(2) + gyro(6)

// --- BLE DEFINITIONS ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// --- Sensor data ---
int16_t ax, ay, az;
int16_t gx, gy, gz;

// --- STATE VARIABLES ---
bool isAlerting = false;
bool mpuInitialized = false;
unsigned long alertStartTime = 0;
const unsigned long VIBRATE_DURATION = 10000; // 10 seconds

// Track button state for single-click detection
int lastButtonState = HIGH;

// Thresholds (same as your working test code)
const float ACCEL_THRESHOLD = 17000.0;
const float GYRO_THRESHOLD  = 3000.0;

// ========== RAW I2C FUNCTIONS (no library needed!) ==========

void mpuWriteByte(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

uint8_t mpuReadByte(uint8_t reg) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1);
  return Wire.read();
}

void mpuReadMotion6() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU6050_ACCEL_XOUT);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)14);
  
  ax = (Wire.read() << 8) | Wire.read();
  ay = (Wire.read() << 8) | Wire.read();
  az = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read(); // skip temperature
  gx = (Wire.read() << 8) | Wire.read();
  gy = (Wire.read() << 8) | Wire.read();
  gz = (Wire.read() << 8) | Wire.read();
}

bool mpuInit() {
  // Wake up the MPU6050 (clear sleep bit)
  mpuWriteByte(MPU6050_PWR_MGMT_1, 0x00);
  delay(100);
  
  // Verify identity
  uint8_t whoAmI = mpuReadByte(MPU6050_WHO_AM_I);
  Serial.printf("WHO_AM_I register returned: 0x%02X\n", whoAmI);
  
  // MPU6050 returns 0x68, MPU6500 returns 0x70, MPU9250 returns 0x71
  return (whoAmI == 0x68 || whoAmI == 0x70 || whoAmI == 0x71);
}

// ========== BLE CALLBACKS ==========

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("PHONE CONNECTED via BLE!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("PHONE DISCONNECTED!");
    }
};

void setup() {
  Serial.begin(115200);
  delay(500);

  // ============================================================
  // STEP 1: Initialize I2C + MPU6050 using RAW Wire commands
  // No MPU6050 library! We talk to the chip directly.
  // This is the same method that the I2C scanner uses (which worked).
  // ============================================================
  Wire.begin(21, 22);
  delay(100);

  Serial.println("Initializing MPU6050 (raw I2C)...");
  if (mpuInit()) {
    Serial.println("MPU6050 connected successfully!");
    mpuInitialized = true;
  } else {
    Serial.println("MPU6050 connection failed!");
  }

  // ============================================================
  // STEP 2: Setup pins
  // ============================================================
  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP); 
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  lastButtonState = digitalRead(BUTTON_PIN);

  // ============================================================
  // STEP 3: Initialize BLE LAST
  // ============================================================
  Serial.println("Starting BLE work!");
  BLEDevice::init("ESP32_SmartAlert");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();
  Serial.println("BLE started! Pair your phone using the app.");
  
  Serial.println("========== SYSTEM READY ==========");
}

void loop() {
  int currentButtonState = digitalRead(BUTTON_PIN);
  bool buttonJustClicked = (currentButtonState == LOW && lastButtonState == HIGH);
  lastButtonState = currentButtonState;

  // --- BLE Connection Management ---
  if (!deviceConnected && oldDeviceConnected) {
      delay(500);
      pServer->startAdvertising();
      Serial.println("Restart advertising");
      oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }

  if (!isAlerting) {
    bool mpuTriggered = false;

    if (mpuInitialized) {
      mpuReadMotion6();

      float accelMagnitude = sqrt((float)ax*ax + (float)ay*ay + (float)az*az);
      float gyroMagnitude = sqrt((float)gx*gx + (float)gy*gy + (float)gz*gz);

      if (accelMagnitude > ACCEL_THRESHOLD || gyroMagnitude > GYRO_THRESHOLD) {
        mpuTriggered = true;
      }
    }

    bool bootPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    if (bootPressed || mpuTriggered) {
      if (bootPressed) {
        Serial.println("BOOT Button Pressed! Starting 10s vibration alert...");
      } else {
        Serial.println("MOVEMENT DETECTED! Starting 10s vibration alert...");
      }
      isAlerting = true;
      alertStartTime = millis();
      digitalWrite(VIBRATION_PIN, HIGH);
      delay(500);
    }
  } 
  else {
    if (buttonJustClicked) {
      Serial.println("Button Clicked. Alarm CANCELLED!");
      digitalWrite(VIBRATION_PIN, LOW);
      isAlerting = false;
      delay(1000);
    } 
    else if (millis() - alertStartTime >= VIBRATE_DURATION) {
      Serial.println("10 Sec Passed. Sending BLUETOOTH SIGNAL to App...");
      digitalWrite(VIBRATION_PIN, LOW);
      
      if (deviceConnected) {
        pCharacteristic->setValue("1");
        pCharacteristic->notify();
        Serial.println("BLE NOTIFY SENT: '1'");
      } else {
        Serial.println("Failed to send: Mobile app is not connected!");
      }

      isAlerting = false; 
      delay(2000);
    }
  }

  delay(200);
}
