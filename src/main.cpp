/*
   Sterilizer puzzle with MQTT support

   In this puzzle, players must adjust three rotary switches to the correct values
   and flip the toggle switch in order to sterilize the test sample before the
   door will release and grant the players one test sample.
    ****DATE***** | ****NAME**** | ****DESCRIPTION****
     2024-04-06   |  R. Nelson   | Initial version to replace Arduino IDE code   
     2024-04-20   |  R. Nelson   | Bug fixes and code cleanup - Tested and working
*/

// Includes

#include <Arduino.h>
#include "arduino_secrets.h"
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <WiFi.h>
#include <FastLED.h>
#include <SPI.h>


// Wifi connection data is in arduino_secrets.h
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int status = WL_IDLE_STATUS;     // the WiFi radio's status


// Create instances of the FastLED library, the Wifi client, and the MQTT client
WiFiClient wifiClient;
PubSubClient MQTTclient(wifiClient);

// DEFINES
#define DEBUG
#define NUM_LEDS 17

CRGB leds[NUM_LEDS];

// Function Declarations
void onSolve();
void onReset();
void mqttCallback(char*, byte*, unsigned int);
void mqttSetup();
void mqttLoop();
//void publish();
void millisdelay(long);
void allonehue (CRGB thehue);
void fadeall();
void looper (CRGB themainhue);
void wifiSetup();



// CONSTANTS
int Rotary = 27;
const int Pump = 25;
const int Flames = 33;
const int MagLock = 26;
int LedBright = 120;

//WIFI Settings
// MAC Address of this device
//const byte mac[] = { 0x84, 0xCC, 0xA8, 0x2F, 0xEF, 0xF8 };

// IP address of the machine on the network running the MQTT broker
const char* mqttServerIP = "10.1.10.55";

// Unique name of this device, used as client ID to connect to MQTT server
// and also topic name for messages published to this device
const char DeviceTopic[] = "ToDevice/Sterilizer";
const char hostTopic[] = "ToHost/Sterilizer";
const char* deviceID = "Sterilizer";



// Global Variables
long lastMsgTime = 0; // The time (from millis()) when the last MQTT message was received
char msg[64]; // A buffer to hold messages to be sent or received
char topic[32]; // The topic in which to publish a message
int pulseCount = 0; // Counter for number of heartbear pulses sent

//int Solved = 0;

enum PuzzleState{Initializing, Running, Solved};
PuzzleState puzzle = Initializing;


// WiFi and MQTT Functions
void wifiSetup(){
  Serial.println();
  Serial.println("****************************");
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("WiFi connected");
  Serial.print("IP Address: ");
  Serial.print(WiFi.localIP());
}

void mqttSetup() {
  MQTTclient.setServer(mqttServerIP, 1883);
  MQTTclient.setCallback(mqttCallback);
  MQTTclient.subscribe(DeviceTopic);
  while (!MQTTclient.connected()) {

    // Debug info
    Serial.println("Attempting to connect to the MQTT broker at ");
    Serial.println(mqttServerIP);
    delay(500); // Slow down the output

    // Attempt to connect
    if (MQTTclient.connect(deviceID)) {

      // Debug info
      Serial.println("Connected to MQTT broker");

      // Once connected, publish an announcement to the host
      MQTTclient.publish(hostTopic, "Sterilizer Connected!");
      // Subscribe to topics meant for this device
      MQTTclient.subscribe(DeviceTopic);
      Serial.println("Subscribed to topic: ");
      Serial.println(DeviceTopic);
      } else {
      // Debug info
      Serial.print("Failed to connect to MQTT broker, rc = ");
      Serial.print(MQTTclient.state());
      Serial.println("Retrying in 5 seconds...");
      // Wait 5 seconds before retrying
      delay(500);
    }
  }
}

void mqttCallback(char* thisTopic, byte* message, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(thisTopic);
  Serial.print("] ");
  Serial.println("Message: ");
  
  // Convert byte array to C-style string
  char messageArrived[length + 1];
  memcpy(messageArrived, message, length);
  messageArrived[length] = '\0'; // Null-terminate the string
 
  for (unsigned int i = 0; i < length; i++) {
    messageArrived[i] = tolower(messageArrived[i]);
  }

  // Act upon the message received
  if (strcasecmp(messageArrived, "Solve") == 0) {
    onSolve();
  }
  else if (strcasecmp(messageArrived, "Reset") == 0) {
    onReset();
  }
  else {
    Serial.print("Message Received: ");
    Serial.println(messageArrived);
    Serial.println(); 
  }
}

void setup() {
  FastLED.addLeds<WS2812B, 14, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(LedBright);

  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // Setup the WiFi and MQTT services
  wifiSetup();
  delay(500); // Slow down the output
  mqttSetup();
  delay(500); // Slow down the output

#ifdef DEBUG
  // Initialize serial communications channel with the PC
  Serial.begin(9600);
#endif
#ifdef DEBUG
  Serial.println("Sterilizer initializing");
  delay(250); // Slow down the output
#endif

  // Set the Rotary switch pin as input
  pinMode(Rotary, INPUT_PULLUP);

  // Set the relay pins as output and ensure locks are magnetized and pump/flames are off
  pinMode(Flames, OUTPUT);
  pinMode(Pump, OUTPUT);
  pinMode(MagLock, OUTPUT);

  digitalWrite(Flames, LOW);
  digitalWrite(Pump, LOW);
  digitalWrite(MagLock, LOW);

  looper( CRGB::Green);
  millisdelay(500);
  looper( CRGB::Blue);
  millisdelay(500);
  looper( CRGB::Red);
  millisdelay(500);

  allonehue( CRGB::Red);                                        // LED turn all  red
  delay(500);
}

void loop() {
  // Switch action based on the current state of the puzzle
  switch(puzzle) {
    {
    case Initializing:
    puzzle = Running;
    break;
    }
    case Running:
    {
      int sensorVal = digitalRead(Rotary);
      //Serial.println(sensorVal);
      // Read the Rotary switches for change
      if (sensorVal == LOW) {
        onSolve();
        Serial.print(F("Sterilizer Solved!"));
      }
      // Call the MQTT loop
      MQTTclient.loop();
      break;
    }

  case Solved: 
    {
    // Trigger the Maglock
    allonehue( CRGB:: Green);
    digitalWrite(MagLock, HIGH);
    digitalWrite(Pump, LOW);
    digitalWrite(Flames, LOW);
    // Call the MQTT loop
    MQTTclient.loop();// put your main code here, to run repeatedly:
    break; // Add break statement here
    }
  }
}

void onSolve () {
#ifdef DEBUG
  Serial.println("Sterilizer has just been solved!");
#endif

  // Trigger the relay for the flames
  digitalWrite(Flames, HIGH);
  // Delay 5 seconds
  delay(5000);
  // Trigger the relay for the pump
  digitalWrite(Pump, HIGH);
  for (int x=0; x<6; x++)
{
  looper( CRGB::Blue );
  //Serial.println("Blue Loop ");
  //Serial.print(x);
}
  // Trigger the Maglock
  allonehue( CRGB:: Green);
  digitalWrite(MagLock, HIGH);
  digitalWrite(Pump, LOW);
  digitalWrite(Flames, LOW);


  // Publish a message to the MQTT broker
  MQTTclient.publish(hostTopic, "Sterilizer puzzle has been solved!");

  // Update the global puzzle state
  puzzle = Solved;
}

void onReset() {
#ifdef DEBUG
  Serial.println("Sterilizer has just been reset!");
#endif
  // Lock the lock, turn off flames, and turn off pump
  digitalWrite(Pump, LOW);
  digitalWrite(Flames, LOW);
  digitalWrite(MagLock, LOW);



  looper( CRGB::Green);
  millisdelay(500);
  looper( CRGB::Blue);
  millisdelay(500);
  looper( CRGB::Red);
  millisdelay(500);


  allonehue(CRGB::Red);                                        // LED turn all  red
  delay(500);


  // Publish a message to the MQTT broker
  MQTTclient.publish(hostTopic, "Sterilizer has been reset!");

  // Update the globale puzzle state
  puzzle = Running;
}

void looper ( CRGB themainhue ) {

  // First slide the led in one direction
  for (int i = 0; i < NUM_LEDS ; i++) {
    leds[i] = themainhue;
    FastLED.show();
    fadeall();
    delay(50);
  }
  // Now go in the other direction
  for (int i = (NUM_LEDS) - 1; i >= 0; i--) {
    leds[i] = themainhue;
    FastLED.show();
    fadeall();
    delay(50);
  }
}

void fadeall() {

  for (int i = 0; i < NUM_LEDS ; i++) {
    leds[i] = CRGB::Black;

  }
}

void allonehue ( CRGB thehue){
  for (int i = 0; i < NUM_LEDS ; i++)
  {
    leds[i] = thehue;
  }
  FastLED.show();
}

void millisdelay(long intervaltime){
  long thetimenow = millis();
  while (millis() < thetimenow + intervaltime)
  {
    // nothing but wait
  }
}

