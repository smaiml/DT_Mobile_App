#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "MAX30100_PulseOximeter.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- DEFINITIONS PROVIDED BY YOU ---
#define SOUND_PIN 34     // HW-484 Sound Sensor DO
#define BUTTON_PIN 25    // Push Button
#define VIBRATION_PIN 26 // Vibration Motor IN
#define BOOT_BUTTON_PIN 0 // Built-in BOOT button

// --- BLE DEFINITIONS ---
// Must match the Flutter app exactly!
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

Adafruit_MPU6050 mpu;
PulseOximeter pox;

// --- STATE VARIABLES ---
bool isAlerting = false;
bool mpuInitialized = false;
bool poxInitialized = false;
unsigned long alertStartTime = 0;
const unsigned long VIBRATE_DURATION = 10000; // 10 seconds (10000 ms)

// Track button state to "click button once" in software
int lastButtonState = HIGH;

// Thresholds to determine if a sensor is "triggered"
const float ACCEL_THRESHOLD_HIGH = 15.0; // Shaking the device (Earth gravity is ~9.8)
const float ACCEL_THRESHOLD_LOW  = 5.0;  // Dropping the device
const int HEART_RATE_THRESHOLD   = 40;   // Any valid heart rate detected

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
  
  // 1. Initialize BLE instead of Classic Bluetooth
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
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("BLE started! Pair your phone using the app.");

  // 2. Setup standard pins
  pinMode(VIBRATION_PIN, OUTPUT);
  digitalWrite(VIBRATION_PIN, LOW); // Start with motor OFF
  
  pinMode(BUTTON_PIN, INPUT_PULLUP); 
  pinMode(SOUND_PIN, INPUT);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  // Initialize tracking state
  lastButtonState = digitalRead(BUTTON_PIN);

  // 3. Initialize I2C lines
  Wire.begin(21, 22);

  // 4. Initialize MPU6050 Giromoter
  Serial.print("Initializing MPU6050... ");
  if (!mpu.begin()) {
    Serial.println("FAILED! Check wiring.");
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
    mpuInitialized = true;
    Serial.println("SUCCESS!");
  }

  // 5. Initialize MAX30100 Heart Sensor
  Serial.print("Initializing MAX30100... ");
  if (!pox.begin()) {
    Serial.println("FAILED! Check wiring.");
  } else {
    Serial.println("SUCCESS!");
    pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
    poxInitialized = true;
  }
  
  Serial.println("========== SYSTEM READY ==========");
}

void loop() {
  // Read our button and detect a literal "click" transition
  int currentButtonState = digitalRead(BUTTON_PIN);
  bool buttonJustClicked = (currentButtonState == LOW && lastButtonState == HIGH);
  lastButtonState = currentButtonState;

  // Manual Override is now handled in the main decision logic below!

  // --- BLE Connection Management ---
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); // give the bluetooth stack the chance to get things ready
      pServer->startAdvertising(); // restart advertising
      Serial.println("Restart advertising");
      oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected; // do stuff here on connecting
  }

  // IMPORTANT: The pulse oximeter must be updated constantly
  if (poxInitialized) {
    pox.update(); 
  }

  if (!isAlerting) {
    int triggeredCount = 0;

    // --- SENSOR 1: Sound Sensor ---
    if (digitalRead(SOUND_PIN) == HIGH) {
      triggeredCount++;
    }

    // --- SENSOR 2: MPU6050 Gyro/Accelerometer ---
    if (mpuInitialized) {
      sensors_event_t a, g, temp;
      mpu.getEvent(&a, &g, &temp);
      float totalAccel = sqrt(a.acceleration.x * a.acceleration.x + 
                              a.acceleration.y * a.acceleration.y + 
                              a.acceleration.z * a.acceleration.z);
      if (totalAccel > ACCEL_THRESHOLD_HIGH || totalAccel < ACCEL_THRESHOLD_LOW) {
        triggeredCount++;
      }
    }
    
    // --- SENSOR 3: MAX30100 Heart Rate ---
    if (poxInitialized) {
      if (pox.getHeartRate() > HEART_RATE_THRESHOLD) {
        triggeredCount++;
      }
    }

    // --- DECISION LOGIC ---
    bool bootPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    if (triggeredCount >= 2 || bootPressed) {
      if (bootPressed) {
        Serial.println("BOOT Button Pressed! Starting 10s vibration alert...");
        // Software auto-click mechanism - ignore the immediate hardware state
      } else {
        Serial.println("ALARM TRIGGERED! (>=2 sensors detected)");
      }
      isAlerting = true;
      alertStartTime = millis();
      digitalWrite(VIBRATION_PIN, HIGH); // Turn motor ON
      delay(500); // 500ms debounce to prevent immediate cancellation if button is held
    }
  } 
  else {
    // --- WE ARE IN THE ALERT STATE ---
    // Use the actual "click" event (transition) instead of the continuous pin state!
    if (buttonJustClicked) {
      Serial.println("Button Clicked. Alarm CANCELLED!");
      digitalWrite(VIBRATION_PIN, LOW); // Stop motor
      isAlerting = false;               // Reset system to listen again
      delay(1000);                      // 1 second debounce
    } 
    else if (millis() - alertStartTime >= VIBRATE_DURATION) {
      Serial.println("10 Sec Passed. Sending BLUETOOTH SIGNAL to App...");
      digitalWrite(VIBRATION_PIN, LOW); // Stop motor
      
      // Send the magic signal to the connected mobile app (App expects "1")
      if (deviceConnected) {
        pCharacteristic->setValue("1");
        pCharacteristic->notify();
        Serial.println("BLE NOTIFY SENT: '1'");
      } else {
        Serial.println("Failed to send: Mobile app is not connected!");
      }

      isAlerting = false; 
      delay(2000); // 2-second cooldown
    }
  }
}
