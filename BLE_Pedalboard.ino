/*
    BLEPedalboard Ben Kazemi

    https://github.com/radioactivetoy/BLEPedalboard

    Based on BLE_notify example by Evandro Copercini.
    Creates a BLE MIDI service and characteristic
    designed for Yamaha THR30II

  Electrical notes:
    Change R4 to 1.6K

    removed r2 and Charger State pin now goes to A2/GPIO 34 // check in code matches LED,  PUT LED BACK

    removed d13 led

    change en to pull down 100k, removed r7
*/

// Bluetooth Includes
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// OLED SSD1306 includes
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>  // Check if needed
#include <Adafruit_SSD1306.h>

//GFX Fonts
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>

#define VBATPIN A13
#define INTERVAL_BAT 2000
#define INTERVAL_PUSHED_PRESET 1300  // Button delay time to prevent flapping - Setting this lower than 1,2s crashes the thr android app
#define INTERVAL_PUSHED_EFFECT 500

#define BUTTON_0 14
#define BUTTON_1 33
#define BUTTON_2 15
#define BUTTON_3 32
#define BUTTON_4 12
#define BUTTON_5 27 // MODE SWITCH

#define PRESET_1 0
#define PRESET_2 4
#define PRESET_3 3
#define PRESET_4 1
#define PRESET_5 2

#define LED_COMP 2
#define LED_GATE 0
#define LED_MOD 3
#define LED_DELAY 4
#define LED_REV 1

#define IS_NOT_CHARGING 34 // open drain, H WHEN charge complete

#define LED_BLUE_0 26 // A0
#define LED_BLUE_1 25 // A1 
#define LED_BLUE_2 4 // A5
#define LED_BLUE_3 5
#define LED_BLUE_4 18

#define LED_GREEN_0 19
#define LED_GREEN_1 16
#define LED_GREEN_2 17
#define LED_GREEN_3 21
#define LED_GREEN_4 13

#define MODE_PRESET false
#define MODE_EFFECT true
#define BUTTON_MODE_CHANGE 5
#define COMP "COMP"
#define GATE "     GATE"
#define MOD "          MOD"
#define DLY "              DLY"
#define REV "                  REV"
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

#define PRESETS 0
#define EFFECTS 1

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Bluetooth Setup
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
bool connected = true;
uint8_t LEDs[2][5] = {{LED_BLUE_0, LED_BLUE_4, LED_BLUE_2, LED_BLUE_1, LED_BLUE_3}, {LED_GREEN_0, LED_GREEN_1, LED_GREEN_2, LED_GREEN_3, LED_GREEN_4}};
// CC Numner for modes 0 (preset) and 1 (effect)
volatile uint8_t ccNumber[5][2] = {{0x14, 0x19}, {0x15, 0x1A}, {0x16, 0x1B}, {0x17, 0x1C}, {0x18, 0x1D}};
volatile bool opMode = 0; // current operation modes 0 (preset) and 1 (effect)
volatile int pushedButton = 0; // Pushed Button
volatile bool sendMidi = false;
volatile bool pushed = false;
volatile bool alreadyInPresetChange = false;
volatile int currentPreset = 1;
float batV = 0;
volatile unsigned long previousBatMillis = 0;
volatile unsigned long previousPushedMillis = 0;

uint8_t midiPacket[] = {
  0x80,  // header
  0x80,  // timestamp, not implemented
  0xb0,  // Continuous controller Chanel 1
  0x14,  // #20
  0x7F   // 7F press 0 Release
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;

    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;

    }
};

void setupBT() {
  // Bluetooth Setup
  BLEDevice::init("Kazemi's Floorboard");

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  // Create the BLE Service
  BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID));
  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      BLEUUID(CHARACTERISTIC_UUID),
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_WRITE_NR
                    );
  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // https://learn.sparkfun.com/tutorials/midi-ble-tutorial/all
  // Create a BLE Descriptor
  pCharacteristic->addDescriptor(new BLE2902());
  // Start the service
  pService->start();
  // Start advertising
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->start();
}

void setup() {
  Serial.begin(115200);
  pinMode(IS_NOT_CHARGING, INPUT_PULLUP); // do something with this in the display

  for (uint8_t i = 0; i < 2; i++) {
    for (uint8_t j = 0; j < 5; j++) {
      pinMode(LEDs[i][j], OUTPUT);
      digitalWrite(LEDs[i][j], HIGH);
    }
  }

  setCpuFrequencyMhz(80); //Set CPU clock to 80MHz fo example

    setupBT();
//  // Bluetooth Setup
//  BLEDevice::init("Kazemi's Floorboard");
//
//  // Create the BLE Server
//  BLEServer *pServer = BLEDevice::createServer();
//  pServer->setCallbacks(new MyServerCallbacks());
//  // Create the BLE Service
//  BLEService *pService = pServer->createService(BLEUUID(SERVICE_UUID));
//  // Create a BLE Characteristic
//  pCharacteristic = pService->createCharacteristic(
//                      BLEUUID(CHARACTERISTIC_UUID),
//                      BLECharacteristic::PROPERTY_READ   |
//                      BLECharacteristic::PROPERTY_WRITE  |
//                      BLECharacteristic::PROPERTY_NOTIFY |
//                      BLECharacteristic::PROPERTY_WRITE_NR
//                    );
//  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
//  // https://learn.sparkfun.com/tutorials/midi-ble-tutorial/all
//  // Create a BLE Descriptor
//  pCharacteristic->addDescriptor(new BLE2902());
//  // Start the service
//  pService->start();
//  // Start advertising
//  BLEAdvertising *pAdvertising = pServer->getAdvertising();
//  pAdvertising->addServiceUUID(pService->getUUID());
//  pAdvertising->start();

  // Display Setup
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    //    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // GPIO Setup
  pinMode(BUTTON_0, INPUT_PULLUP);
  pinMode(BUTTON_1, INPUT_PULLUP);
  pinMode(BUTTON_2, INPUT_PULLUP);
  pinMode(BUTTON_3, INPUT_PULLUP);
  pinMode(BUTTON_4, INPUT_PULLUP);
  pinMode(BUTTON_5, INPUT_PULLUP);
  // Interrputs on button push  attachInterrupt(digitalPinToInterrupt(BUTTON_0), buttonEvent_0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_0), buttonEvent_0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_1), buttonEvent_1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_2), buttonEvent_2, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_3), buttonEvent_3, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_4), buttonEvent_4, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_5), buttonEvent_5, FALLING);
}


void loop() {
  getBat();

  if (deviceConnected) {
//    First screen update / uknown patch
        if (!connected) {
          connected = true;
          display.clearDisplay();
          display.setCursor(50, 20);
          display.setTextSize(5);
          display.setTextColor(WHITE);
          display.println("?");
          printBat();
          display.display();
          for (uint8_t i = 0; i < 2; i++) {
            for (uint8_t j = 0; j < 5; j++) {
              pinMode(LEDs[i][j], OUTPUT);
              digitalWrite(LEDs[i][j], LOW);
            }
          }
          //setupBT();
        }

    if (pushed) {
      volatile boolean isLegit = false;
      if (pushedButton == BUTTON_MODE_CHANGE && (millis() - previousPushedMillis >= 75)) { // changing mode debounce fast
        opMode = !opMode;
        isLegit = true;
      } else if (opMode == MODE_PRESET && pushedButton < 5  && (millis() - previousPushedMillis >= INTERVAL_PUSHED_PRESET)) {  // changing preset
        currentPreset = pushedButton + 1;
        isLegit = true;
      } else if (opMode == MODE_EFFECT && pushedButton != BUTTON_MODE_CHANGE  && (millis() - previousPushedMillis >= INTERVAL_PUSHED_EFFECT)) { // changing effect
        isLegit = true;
      }

      if (opMode == MODE_PRESET && isLegit) { //show preset
        if (alreadyInPresetChange) {
          for (int i = 0; i < 5; i++) {
            digitalWrite(LEDs[PRESETS][i], LOW);
          }
        } else
          alreadyInPresetChange = true;
        for (int i = 0; i < 5; i++) {
          digitalWrite(LEDs[EFFECTS][i], LOW);
        }
        illPres(currentPreset - 1);
        display.clearDisplay();
        display.setCursor(50, 20);
        display.setTextSize(5);
        display.setTextColor(WHITE);
        display.println(currentPreset);
        display.setCursor(40, 55);
        display.setTextSize(1);
        display.setCursor(0, 9);
        display.setFont(&FreeSerif12pt7b);
        display.println("Preset");
        display.setFont();
        printBat();
        display.display();
      } else if (opMode == MODE_EFFECT && isLegit) {  // show effects
        selectedFX();
        alreadyInPresetChange = false;
        for (int i = 0; i < 5; i++) {
          digitalWrite(LEDs[EFFECTS][i], HIGH);
        }
        display.clearDisplay();
        display.setCursor(55, 16);
        display.setTextSize(4);
        display.setTextColor(WHITE);
        display.println(currentPreset);
        display.setCursor(0, 56);
        display.setTextSize(1);
        display.println("COMP GATE MOD DLY REV"); // 21 chars
        display.setCursor(0, 9);
        display.setFont(&FreeSerif12pt7b);
        display.println("FX");
        display.setFont();
        printBat();
        display.display();
      }

      if (sendMidi && isLegit)  {  // send MIDI data
        // Send Push
        midiPacket[3] = ccNumber[pushedButton][opMode]; // test code, change to pushedButton
        midiPacket[4] = 0x7F; // CC Max Value
        pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
        pCharacteristic->notify();
        /**
          // Send release - Not needed on the THR
          midiPacket[4]=0x00; // CC Min Value (Release)
          pCharacteristic->setValue(midiPacket, 5); // packet, length in bytes
          pCharacteristic->notify();
        **/
      }
      sendMidi = false;  // clear flags even if not legit triggers
      pushed = false;
      if (isLegit) { // save legit trigger times
        previousPushedMillis = millis();  // take last legit reading
        //        if (opMode == MODE_EFFECT && pushedButton != BUTTON_MODE_CHANGE)
        //
      }
      interrupts();
    }
  } else if (connected) { // we've disconnected!
    connected = false;
    display.clearDisplay();
    display.setCursor(4, 9);
    display.setFont(&FreeSerif12pt7b);
    display.println("Kazemi..");
    display.setFont(&FreeSerifItalic12pt7b);
    display.setCursor(1, 31);
    display.println("Connect");
    display.println("      Bluetooth");
    printBat();
    display.display();
    for (uint8_t i = 0; i < 2; i++) {
      for (uint8_t j = 0; j < 5; j++) {
        pinMode(LEDs[i][j], OUTPUT);
        digitalWrite(LEDs[i][j], HIGH);
      }
    }
    //deviceConnected = false;
//    while (deviceConnected) {delay(100);}
//    setupBT();
  }
}


/**
   process common functionality for the button interrupts

   @param sentMidi are we sending midi
   @param pushedBut which button have we pushed

*/
void buttonEvent(volatile boolean sentMidi, volatile int pushedBut) {
  noInterrupts(); // we don't want button pushes to change boolean flags when we're processing existing trigger
  if (sentMidi)
    sendMidi = true;
  pushedButton = pushedBut;
  pushed = true;
}

void  buttonEvent_0() {
  buttonEvent(true, 0);
}

void  buttonEvent_1() {
  buttonEvent(true, 1);
}

void  buttonEvent_2() {
  buttonEvent(true, 2);
}

void  buttonEvent_3() {
  buttonEvent(true, 3);
}

void buttonEvent_4() {
  buttonEvent(true, 4);
}

void  buttonEvent_5() {
  buttonEvent(false, 5);
}

/**
   illumiunate the preset leds
*/
void illPres(int but) {
  switch (pushedButton) {
    case 0:
      digitalWrite(LEDs[PRESETS][PRESET_1], HIGH);
      break;
    case 1:
      digitalWrite(LEDs[PRESETS][PRESET_2], HIGH);
      break;
    case 2:
      digitalWrite(LEDs[PRESETS][PRESET_3], HIGH);
      break;
    case 3:
      digitalWrite(LEDs[PRESETS][PRESET_4], HIGH);
      break;
    case 4:
      digitalWrite(LEDs[PRESETS][PRESET_5], HIGH);
  }
}
/**
   Momentarily show the effect you just triggered
   This uses a blocking delay which is acceptable since the duration is lower than the non blocking delay that accepts a new instruction
*/
void selectedFX() {
  display.setCursor(0, 56);
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  display.println("                     "); // 21 chars
  display.setTextColor(WHITE);
  display.setCursor(0, 56);
  for (int i = 0; i < 5; i++) {
    digitalWrite(LEDs[EFFECTS][i], LOW);

  }
  switch (pushedButton) {
    case 0:
      display.println(COMP); // 21 chars
      digitalWrite(LEDs[EFFECTS][LED_COMP], HIGH);
      break;
    case 1:
      display.println(GATE); // 21 chars
      digitalWrite(LEDs[EFFECTS][LED_GATE], HIGH);
      break;
    case 2:
      display.println(MOD); // 21 chars
      digitalWrite(LEDs[EFFECTS][LED_MOD], HIGH);
      break;
    case 3:
      display.println(DLY); // 21 chars
      digitalWrite(LEDs[EFFECTS][LED_DELAY], HIGH);
      break;
    case 4:
      display.println(REV); // 21 chars
      digitalWrite(LEDs[EFFECTS][LED_REV], HIGH);
  }
  display.display();
  delay(250);
  display.setCursor(0, 56);
  display.println("COMP GATE MOD DLY REV"); // 21 chars
  display.display();
  for (int i = 0; i < 5; i++) {
    digitalWrite(LEDs[EFFECTS][i], HIGH);
  }

}

/**
   Measure a new voltage reading for the attached battery and store it
   The reference voltage changes depending on the power source
*/
void getBat() {
  if (millis() - previousBatMillis >= INTERVAL_BAT) {
    previousBatMillis = millis();
    batV = analogRead(VBATPIN);
    batV /= 4095; // convert to voltage
    batV *= 2;    // we divided by 2, so multiply back
    batV *= 3.177;  // Multiply by 3.3V, our reference voltage (this changes depending on power source)
    batV *= 1.107;  // Multiply by 1.1V, our ADC reference voltage
    printBat();
  }
}

/**
   print the global variable holding the battery voltage to the OLED correctly formatted
*/
void printBat() {
  display.setFont();
  display.setTextSize(1);
  display.setCursor(80, 0);
  display.setTextColor(WHITE, BLACK);
  display.print("        ");
  display.setTextColor(WHITE);
  if (digitalRead(IS_NOT_CHARGING) || batV <= 4.25) {
    display.setCursor(92, 0);
    display.print(batV);
    display.println(" V");
  }
  else if (!digitalRead(IS_NOT_CHARGING) && batV < 4.25) {
    display.setCursor(80, 0);
    display.println("Charging");
  }
  else if (!digitalRead(IS_NOT_CHARGING) && batV >= 4.25) {
    display.setCursor(80, 0);
    display.println("Charged");
  }


  // debug
  display.setCursor(92, 24);
  display.setTextColor(WHITE, BLACK);
  display.print("        ");
  display.setTextColor(WHITE);
  display.setCursor(92, 24);
  display.print(batV);
  display.println(" V");
  display.setCursor(92, 32);
  display.setTextColor(WHITE, BLACK);
  display.print("        ");
  display.setTextColor(WHITE);
  display.setCursor(92, 32);
  display.print(analogRead(IS_NOT_CHARGING));
  // debug


  display.display();
}
