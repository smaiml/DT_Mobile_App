#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// These UUIDs match exactly what your Flutter app is looking for
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// We will use the built-in BOOT button (GPIO 0 on most ESP32 boards) to trigger an alert
// For ESP32-WROOM/WROVER, the BOOT button is GPIO 0. It is LOW when pressed.
const int buttonPin = 0; 
bool lastButtonState = HIGH; 

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("App Connected!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("App Disconnected...");
    }
};

void setup() {
  Serial.begin(115200);
  delay(2000); // Give Serial Monitor time to connect

  // Configure the button pin
  pinMode(buttonPin, INPUT_PULLUP);

  // Initialize the Bluetooth device with a name
  BLEDevice::init("DT Safety ESP32");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY 
                    );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("ESP32 Started!");
  Serial.println("Waiting for mobile app to connect via Bluetooth...");
}

void loop() {
    // Notify changed value (if button is pressed and mobile app is connected)
    if (deviceConnected) {
        bool currentButtonState = digitalRead(buttonPin);
        
        // Check if button was just pressed (transition from HIGH to LOW)
        if (lastButtonState == HIGH && currentButtonState == LOW) {
            Serial.println("Alert Button Pressed! Sending signal to phone...");

            // The flutter app is specifically looking for the string "1"
            String emergencySignal = "1";
            
            pCharacteristic->setValue(emergencySignal.c_str());
            pCharacteristic->notify();
            
            delay(500); // Debounce delay
        }
        
        lastButtonState = currentButtonState;
    }

    // Handle disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("Disconnected, restarting advertising...");
        oldDeviceConnected = deviceConnected;
    }
    
    // Handle connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
    }
    
    delay(50); // Small delay to prevent watchdog panic
}
