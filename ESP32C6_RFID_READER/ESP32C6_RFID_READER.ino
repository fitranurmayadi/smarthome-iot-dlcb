#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_PN532.h>

//------------------------------------------------------------------------------
// WiFi & MQTT Configuration
//------------------------------------------------------------------------------

const char* ssid 		= "YOURWIFISSID";
const char* password 		= "YOURWIFIPASSWORD";
const char* mqtt_server		= "YOURHOSTIP";
const int mqtt_port 		= 1883;
const char* mqtt_user 		= "YOURMQTTUSERNAME";
const char* mqtt_password 	= "YOURMQTTPASSWORD";

// MQTT topics for solenoid control and state reporting
const char* mqtt_command_topic = "esp32/solenoid/set";
const char* mqtt_state_topic   = "esp32/solenoid/state";

//------------------------------------------------------------------------------
// PN532 (I2C bit-bang) Pin Settings
//------------------------------------------------------------------------------
#define SDA_PIN 6  // GPIO pin for PN532 SDA (software I2C)
#define SCL_PIN 7  // GPIO pin for PN532 SCL (software I2C)

// Create an Adafruit_PN532 instance using software I2C on SDA_PIN and SCL_PIN.
Adafruit_PN532 nfc(SDA_PIN, SCL_PIN);

// Create WiFi and MQTT client objects.
WiFiClient espClient;
PubSubClient client(espClient);

//------------------------------------------------------------------------------
// LED RGB Pin Settings
//------------------------------------------------------------------------------
#define LED_RED_PIN    23  // GPIO pin for red LED channel
#define LED_GREEN_PIN  22  // GPIO pin for green LED channel
#define LED_BLUE_PIN   21  // GPIO pin for blue LED channel

// Variables to manage LED blinking state and timing.
unsigned long lastBlinkTime = 0;  // Timestamp of last blue LED toggle
bool ledBlueState          = false;  // Current on/off state for blue LED
bool idleState             = true;   // Indicates whether the system is in idle mode

//------------------------------------------------------------------------------
// Buzzer Pin
//------------------------------------------------------------------------------
#define BUZZER_PIN 10  // GPIO pin for buzzer output

//------------------------------------------------------------------------------
// Authorized RFID Tags
//------------------------------------------------------------------------------
// Each entry is an array of UID bytes. Length of each UID is stored separately.
const uint8_t allowedTags[][7] = {
  { 0xA3, 0xB1, 0x83, 0xFA },
  { 0xF2, 0xB4, 0x49, 0x73 },
  { 0x12, 0x34, 0x56, 0x78, 0x90 }
};
const uint8_t allowedTagsLengths[] = { 4, 4, 5 };
const uint8_t allowedTagsCount    = sizeof(allowedTags) / sizeof(allowedTags[0]);

//------------------------------------------------------------------------------
// Function Prototypes
//------------------------------------------------------------------------------
void setLEDColor(bool granted, bool denied);
bool checkTagAllowed(const uint8_t* uid, uint8_t length);
void beepBuzzer();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT();

//------------------------------------------------------------------------------
// Function: setLEDColor
//
// Controls the RGB LED based on the access outcome.
//
// Parameters:
//   granted - true if access is granted (turn on green LED).
//   denied  - true if access is denied (turn on red LED).
//             If both are false, LED enters idle mode (blue blinking).
//------------------------------------------------------------------------------
void setLEDColor(bool granted, bool denied) {
  if (granted) {
    // Access granted → Turn on green LED, turn off red/blue
    digitalWrite(LED_RED_PIN,   LOW);
    digitalWrite(LED_GREEN_PIN, HIGH);
    digitalWrite(LED_BLUE_PIN,  LOW);
  } else if (denied) {
    // Access denied → Turn on red LED, turn off green/blue
    digitalWrite(LED_RED_PIN,   HIGH);
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_BLUE_PIN,  LOW);
  } else {
    // Idle mode → Toggle blue LED based on ledBlueState
    digitalWrite(LED_RED_PIN,   LOW);
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_BLUE_PIN,  ledBlueState ? HIGH : LOW);
  }
}

//------------------------------------------------------------------------------
// Function: checkTagAllowed
//
// Compares a scanned UID against the list of authorized UIDs.
//
// Parameters:
//   uid    - Pointer to the array of scanned bytes.
//   length - Number of bytes in the scanned UID.
//
// Returns:
//   true if the UID matches one in allowedTags, false otherwise.
//------------------------------------------------------------------------------
bool checkTagAllowed(const uint8_t* uid, uint8_t length) {
  for (uint8_t i = 0; i < allowedTagsCount; i++) {
    if (allowedTagsLengths[i] == length) {
      bool match = true;
      for (uint8_t j = 0; j < length; j++) {
        if (uid[j] != allowedTags[i][j]) {
          match = false;
          break;
        }
      }
      if (match) return true;
    }
  }
  return false;
}

//------------------------------------------------------------------------------
// Function: beepBuzzer
//
// Emits two short beeps on the buzzer to indicate a card scan event.
//------------------------------------------------------------------------------
void beepBuzzer() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

//------------------------------------------------------------------------------
// Function: mqttCallback
//
// Unused placeholder for incoming MQTT messages.
//------------------------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // No commands handled in this example
}

//------------------------------------------------------------------------------
// Function: reconnectMQTT
//
// Attempts to reconnect to the MQTT broker if disconnected.
// Retries every 5 seconds until a connection is established.
// Subscribes to the command topic upon successful connection.
//------------------------------------------------------------------------------
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke MQTT...");
    if (client.connect("esp32_rfid_client", mqtt_user, mqtt_password)) {
      Serial.println("terhubung");
      client.setCallback(mqttCallback);
      client.subscribe(mqtt_command_topic);
    } else {
      Serial.print("gagal, rc=");
      Serial.print(client.state());
      Serial.println(" coba lagi dalam 5 detik...");
      delay(5000);
    }
  }
}

//------------------------------------------------------------------------------
// Function: setup
//
// Initializes serial, PN532 module, RGB LED pins, buzzer, WiFi, and MQTT client.
// Sets the initial LED state for idle blinking.
//------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Initialize PN532 (software I2C) and check for firmware version
  nfc.begin();
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 tidak terdeteksi! Periksa wiring.");
    while (1) {
      delay(1000);
    }
  }
  Serial.print("PN532 Firmware: ");
  Serial.print((versiondata >> 24) & 0xFF, HEX);
  Serial.print(".");
  Serial.print((versiondata >> 16) & 0xFF, HEX);
  Serial.print(".");
  Serial.println((versiondata >> 8) & 0xFF, HEX);
  nfc.SAMConfig();

  // Initialize RGB LED pins and ensure all off initially
  pinMode(LED_RED_PIN,   OUTPUT);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_BLUE_PIN,  OUTPUT);
  digitalWrite(LED_RED_PIN,   LOW);
  digitalWrite(LED_GREEN_PIN, LOW);
  digitalWrite(LED_BLUE_PIN,  LOW);

  // Initialize buzzer pin and emit startup beep
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  beepBuzzer();

  // Connect to WiFi network
  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung: " + WiFi.localIP().toString());

  // Configure MQTT server for future reconnect attempts
  client.setServer(mqtt_server, mqtt_port);
  Serial.println("Siap menunggu scan RFID...");

  // Prepare LED for immediate blue blinking in idle mode
  ledBlueState   = true;
  setLEDColor(false, false);
  lastBlinkTime  = millis();
  idleState      = true;
}

//------------------------------------------------------------------------------
// Function: loop
//
// Main loop does two tasks:
// 1. Attempts to read an RFID tag via PN532. If tag detected, processes access logic:
//    - Displays UID on Serial.
//    - Beeps buzzer.
//    - Checks authorization, sets LED (green/red), sends MQTT messages.
//    - Resets to idle state and timer after processing.
//
// 2. If no tag is detected (idle), toggles blue LED every 2 seconds.
//
// Also ensures MQTT connection is maintained.
//------------------------------------------------------------------------------
void loop() {
  // Ensure MQTT connection is alive
  if (!client.connected()) reconnectMQTT();
  client.loop();

  // Buffer for scanned UID
  uint8_t uid[7];
  uint8_t uidLength;

  // 1) Check for an RFID tag. If present, process access immediately.
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
    Serial.println("=== TAG DETECTED ===");
    idleState = false;

    // (a) Print UID and beep buzzer
    Serial.print("UID: ");
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) Serial.print("0");
      Serial.print(uid[i], HEX);
      if (i != uidLength - 1) Serial.print(" ");
    }
    Serial.println();
    beepBuzzer();

    // (b) Check authorization, set LED, and publish MQTT messages
    bool allowed = checkTagAllowed(uid, uidLength);
    setLEDColor(allowed, !allowed);
    if (allowed) {
      Serial.println("Akses diberikan");
      client.publish(mqtt_command_topic, "ON", true);
      client.publish(mqtt_state_topic,   "ON", true);
      delay(3000);
      client.publish(mqtt_command_topic, "OFF", true);
      client.publish(mqtt_state_topic,   "OFF", true);
    } else {
      Serial.println("Akses ditolak");
      delay(1000);
    }

    // (c) Reset to idle mode for blue blinking
    idleState    = true;
    ledBlueState = true;             // Prepare blue LED ON next toggle
    setLEDColor(false, false);       // Turn off red/green, show blue=LOW initially
    lastBlinkTime = millis();        // Reset timer so blinking starts fresh
  }
  else {
    // 2) Idle mode: toggle blue LED every 2 seconds if no tag is present
    if (millis() - lastBlinkTime > 2000) {
      Serial.println("→ Idle mode: toggle LED biru");
      ledBlueState = !ledBlueState;
      setLEDColor(false, false);
      lastBlinkTime = millis();
    }
  }
}
