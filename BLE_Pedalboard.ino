/*
    BLE_MIDI Example by neilbags
    https://github.com/neilbags/arduino-esp32-BLE-MIDI

    Based on BLE_notify example by Evandro Copercini.
    Creates a BLE MIDI service and characteristic.
    Once a client subscribes, send a MIDI message every 2 seconds
*/
#define VBATPIN A13
#define INTERVAL_BAT 2000
#define INTERVAL_PUSHED_PRESET 1300  // Button delay time to prevent flapping - Setting this lower than 1,2s crashes the thr android app
#define INTERVAL_PUSHED_EFFECT 350
#define BUTTON_0 12
#define BUTTON_1 14
#define BUTTON_2 32
#define BUTTON_3 27
#define BUTTON_4 15
#define BUTTON_5 33
#define MODE_PRESET false
#define MODE_EFFECT true
#define BUTTON_MODE_CHANGE 5
#define COMP "COMP"
#define GATE "     GATE"
#define MOD "          MOD"
#define DLY "              DLY"
#define REV "                  REV"

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

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define SERVICE_UUID        "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Bluetooth Setup
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
bool connected = true;

// CC Numner for modes 0 (preset) and 1 (effect)
uint8_t ccnumber[5][2] = {{0x14, 0x19}, {0x15, 0x1A}, {0x16, 0x1B}, {0x17, 0x1C}, {0x18, 0x1D}};
bool opmode = 0; // current operation modes 0 (preset) and 1 (effect)
volatile int pushedbutton = 0; // Pushed Button
volatile bool sendmidi = false;
volatile bool pushed = false;
int currpreset = 1;

float measuredvbat = 0;

volatile boolean inMidiSend = false;
unsigned long previousBatMillis = 0;
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
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

void setup() {
//  Serial.begin(115200);

  setCpuFrequencyMhz(80); //Set CPU clock to 80MHz fo example

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
  // Interrputs on button push  attachInterrupt(digitalPinToInterrupt(BUTTON_0), buttonevent_0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_0), buttonevent_0, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_1), buttonevent_1, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_2), buttonevent_2, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_3), buttonevent_3, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_4), buttonevent_4, FALLING);
  attachInterrupt(digitalPinToInterrupt(BUTTON_5), buttonevent_5, FALLING);
}

void loop() {
  getBat();

  if (deviceConnected) {
    //First screen update / uknown patch
    if (!connected) { 
      connected = true;
      display.clearDisplay();
      display.setCursor(50, 20);
      display.setTextSize(5);
      display.setTextColor(WHITE);
      display.println("?");            
      printBat();
      display.display();
    }

    if (pushed) {
      boolean isLegit = false;  
      if (pushedbutton == BUTTON_MODE_CHANGE && (millis() - previousPushedMillis >= 75)) { // changing mode debounce fast 
        opmode = !opmode;
        isLegit = true;
      } else if (opmode == MODE_PRESET && pushedbutton < 5  && (millis() - previousPushedMillis >= INTERVAL_PUSHED_PRESET)) {  // changing preset 
        currpreset = pushedbutton + 1;
        isLegit = true;
      } else if (opmode == MODE_EFFECT && pushedbutton != BUTTON_MODE_CHANGE  && (millis() - previousPushedMillis >= INTERVAL_PUSHED_EFFECT)) { // changing effect
        isLegit = true;    
      }

      if (opmode == MODE_PRESET && isLegit) { //show preset
        display.clearDisplay();
        display.setCursor(50, 20);
        display.setTextSize(5);
        display.setTextColor(WHITE);
        display.println(currpreset);
        display.setCursor(40, 55);
        display.setTextSize(1);
        display.setCursor(0, 9);
        display.setFont(&FreeSerif12pt7b);
        display.println("Preset");
        display.setFont();
        printBat();
        display.display();
      } else if (opmode == MODE_EFFECT && isLegit) {  // show effects
        display.clearDisplay();
        display.setCursor(55, 16);
        display.setTextSize(4);
        display.setTextColor(WHITE);
        display.println(currpreset);
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

      if (sendmidi && isLegit)  {  // send MIDI data
        // Send Push
        midiPacket[3] = ccnumber[pushedbutton][opmode]; // test code, change to pushedbutton
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
      sendmidi = false;  // clear flags even if not legit triggers
      pushed = false;
      if (isLegit) { // save legit trigger times
        previousPushedMillis = millis();  // take last legit reading 
        if (opmode == MODE_EFFECT && pushedbutton != BUTTON_MODE_CHANGE)
          selectedFX();
      }
      interrupts();
    }
  } else if (connected) { // Update the display only when Connectio status is changed
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
  }
}

void _pushed(volatile boolean sentMidi, volatile int pushedBut) {
  noInterrupts(); // we don't want button pushes to change boolean flags when we're processing existing trigger
  if (sentMidi)
    sendmidi = true;
  pushedbutton = pushedBut;
  pushed = true;
}

void  buttonevent_0() {
  _pushed(true, 0);
}

void  buttonevent_1() {
  _pushed(true, 1);
}

void  buttonevent_2() {
  _pushed(true, 2);
}

void  buttonevent_3() {
  _pushed(true, 3);
}

void buttonevent_4() {
  _pushed(true, 4);
}

void  buttonevent_5() {
  _pushed(false, 5);
}

void getBat() {  
  if (millis() - previousBatMillis >= INTERVAL_BAT) {
    previousBatMillis = millis();
    measuredvbat = analogRead(VBATPIN);
    measuredvbat /= 4095; // convert to voltage
    measuredvbat *= 2;    // we divided by 2, so multiply back
    measuredvbat *= 3.177;  // Multiply by 3.3V, our reference voltage (this changes depending on power source)
    measuredvbat *= 1.107;  // Multiply by 1.1V, our ADC reference voltage
    printBat();
  }
}

void selectedFX() {
  display.setCursor(0, 56);
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  display.println("                     "); // 21 chars
  display.setTextColor(WHITE);
  display.setCursor(0, 56);
  switch (pushedbutton) {
    case 0:
      display.println(COMP); // 21 chars
      break;
    case 1:
      display.println(GATE); // 21 chars
      break;
    case 2:
      display.println(MOD); // 21 chars
      break;
    case 3:
      display.println(DLY); // 21 chars
      break;
    case 4:
      display.println(REV); // 21 chars
  }
  display.display();
  delay(250);
  display.setCursor(0, 56);
  display.println("COMP GATE MOD DLY REV"); // 21 chars
  display.display();

}

void printBat() {
  display.setFont();
  display.setTextSize(1);
  display.setCursor(92, 0);
  display.setTextColor(WHITE, BLACK);
  display.print("      ");
  display.setCursor(92, 0);
  display.setTextColor(WHITE);
  display.print(measuredvbat);
  display.println(" V");
  display.display();
}
