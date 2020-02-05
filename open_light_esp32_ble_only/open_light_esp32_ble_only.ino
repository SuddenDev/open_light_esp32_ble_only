#include "ArduinoJson.h"
#include <Preferences.h>
#include <SimpleCLI.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// LED STUFF
double brightnessVal;
double temperatureVal;
bool power;
int warmVal;
int coolVal;
int old_warmVal = 0;
int old_coolVal = 0;
int dutycyleWarm;
int dutycyleCool;


// setting PWM properties
const int freq = 5000;
const int ledChannel0 = 0;
const int ledChannel1 = 1;
const int resolution = 8;

const int ledPinWarm = 15;  // 15 corresponds to GPIO15
const int ledPinCool = 16;  // 15 corresponds to GPIO15


// persistant preferences 
Preferences preferences;

// Create CLI Object
SimpleCLI cli;

// Commands
Command getLightStatus;
Command setLightStatus;

#define SERVICE_UUID "811cc2f6-98b0-40d5-ae1f-f8f547c5b243"
#define CHARACTERISTIC_UUID "019160c0-c9d2-4558-8776-75eb305bed64"
#define DEVICE_NAME "aline"

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool BLE_deviceConnected = false;
bool BLE_oldDeviceConnected = false;




/**
 * Sending Status of leds
 */
void sendStatus() {
  Serial.println("Sending Status");
     

  // MAKESHIFT solution, kinda hacky: al:app->light ; la: light->app
  String output = "la," + String(brightnessVal) + "," + String(temperatureVal)  + "," + String(power);

  Serial.println("Notifying: ");
  Serial.println(output);

  delay(150);
  pCharacteristic->setValue(output.c_str());
  pCharacteristic->notify();
}

// get light status
void getStatusCommand(cmd* c) {
  Command cmd(c);
  sendStatus();
}

// setting state
void setStatusCommand(cmd* c) {
  Command cmd(c);
  Argument stateArg = cmd.getArgument("state");
  String stateValue = stateArg.getValue();
  setLight(stateValue);
}



void setLight(String value) {
  if(split(value, ',',0) != "al") return;

  brightnessVal = split(value, ',',1).toDouble();
  temperatureVal = split(value, ',',2).toDouble();
  power = split(value, ',',3).toInt();

  Serial.print("brightnessVal:");
  Serial.println(brightnessVal);

  Serial.print("temperatureVal:");
  Serial.println(temperatureVal);
  
  Serial.print("power:");
  Serial.println(power);

  // calculate brightness values for leds
  int brightness = (int) pow(lerp(0.0, 16.0, brightnessVal), 2);
  Serial.print("brightness:");
  Serial.println(brightness);


  // :::::::::::::TODO:::::::::::
  warmVal = (int) brightness * temperatureVal;
  coolVal = (int) brightness * (1.0 - temperatureVal);

  if(warmVal > 255) warmVal = 255;
  if(coolVal > 255) coolVal = 255;

  if(power == 1) {
    Serial.print("Warm:");
    Serial.println(warmVal);
    fade(old_warmVal, warmVal, dutycyleWarm, ledChannel0);
    Serial.print("Cool:");
    Serial.println(coolVal);
    fade(old_coolVal, coolVal, dutycyleCool, ledChannel1);

    old_warmVal = warmVal;
    old_coolVal = coolVal;
  } else {
    fade(old_warmVal, 0, dutycyleWarm, ledChannel0);
    fade(old_coolVal, 0, dutycyleCool, ledChannel1);
  }


  // Save preferences
  //preferences.begin("aline", false);
  //brightnessVal = preferences.putDouble("brightnessVal", brightnessVal);
  //temperatureVal = preferences.putDouble("temperatureVal", temperatureVal);
  //coolVal = preferences.putInt("coolVal", coolVal);
  //warmVal = preferences.putInt("warmVal", warmVal);
  //power = preferences.putBool("power", power);
  //preferences.end();
}



class ol_BTServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) {
    BLE_deviceConnected = true;
    Serial.println("BLE Device connected");
    BLEDevice::startAdvertising();
  };

  void onDisconnect(BLEServer *pServer) {
    BLE_deviceConnected = false;
    Serial.println("BLE Device disconnected");
  }
};

class ol_BTCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.length() > 0) {
      Serial.print("Value : ");
      Serial.println(value.c_str());

      cli.parse(value.c_str());
      Command cmd = cli.getCmd();
    
      setLight(value.c_str());
    }
    
  }
};

void setup() {
  Serial.begin(115200);

  // Open Preferences with aline namespace.
  preferences.begin("aline", false);

  brightnessVal = preferences.getDouble("brightnessVal", 0.7);
  temperatureVal = preferences.getDouble("temperatureVal", 0.56);
  coolVal = preferences.getInt("coolVal", 128);
  warmVal = preferences.getInt("warmVal", 128);
  power = preferences.getBool("power", true);

  getLightStatus = cli.addCmd("getLightStatus", getStatusCommand);
  setLightStatus = cli.addCmd("setLightStatus", setStatusCommand);

  setLightStatus.addPositionalArgument("state");

  //preferences.putInt("modeIdx", modeIdx);

  // close preferences
  preferences.end();

  // configure LED PWM functionalitites
  ledcSetup(ledChannel0, freq, resolution);
  ledcSetup(ledChannel1, freq, resolution);
  
  // attach the channel to the GPIO to be controlled
  ledcAttachPin(ledPinWarm, ledChannel0);
  ledcAttachPin(ledPinCool, ledChannel1);

  fade(0.0, warmVal, dutycyleWarm, ledChannel0);
  fade(0.0, coolVal, dutycyleCool, ledChannel1);

  // run ble task
  bleTask();

}

/*
   BLEUTOOTH TASK: Creating and enabling a Bluetooth server, ready for connecting
   @return void
*/
void bleTask()
{
  // Create the BLE Device
  BLEDevice::init(DEVICE_NAME);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ol_BTServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY
      //BLECharacteristic::PROPERTY_INDICATE
  );

  pCharacteristic->setCallbacks(new ol_BTCallbacks());
  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0); // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}


/*
   handleBLEConnections:
   Checking the Bluetooth connection and starting advertising if not connected
   @return void
*/
void handleBLEConnections() {
  if (!BLE_deviceConnected && BLE_oldDeviceConnected) {
    delay(500);                  // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("start advertising");
    BLE_oldDeviceConnected = BLE_deviceConnected;
  }
  
  // connecting
  if (BLE_deviceConnected && !BLE_oldDeviceConnected) {
    // do stuff here on connecting
    BLE_oldDeviceConnected = BLE_deviceConnected;
  }
}

// standard float operation
double lerp(double a, double b, double f) {
  return a + f * (b - a);
}

// Fade effect
void fade(int oldVal, int newVal, int dutyCycle, int ledChannel) {
  if(oldVal <= newVal) {
    for(int i = oldVal; i <= newVal; i++) {
      if(i <= 255 && i >= 0) {
        dutyCycle = i;
        ledcWrite(ledChannel, dutyCycle);
      }
    }
  } else {
    for(int i = oldVal; i >= newVal; i--) {
      if(i <= 255 && i >= 0) {
        dutyCycle = i;
        ledcWrite(ledChannel, dutyCycle);
      }
    }
  }
}

// Split function  
String split(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for(int i=0; i<=maxIndex && found<=index; i++){
    if(data.charAt(i)==separator || i==maxIndex){
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found>index ? data.substring(strIndex[0], strIndex[1]) : "";
}





/**
 * -------------------------------------------------
 * MAIN THREAD
 * -------------------------------------------------
 */

void loop() {
  handleBLEConnections();

  if (BLE_deviceConnected) {
    // DO SOMETHING
  }

  if(Serial.available()) {
      String input = Serial.readStringUntil('\n');
  
      // Echo the user input
      Serial.print("# ");
      Serial.println(input);
  
      // Parse the user input into the CLI
      cli.parse(input);
  }
 
}
