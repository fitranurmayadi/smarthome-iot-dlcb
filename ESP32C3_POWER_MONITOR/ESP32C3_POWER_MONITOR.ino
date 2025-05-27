#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <PZEM004Tv30.h>

// -----------------------------------------------------------------------------
// CONFIGURATION
// -----------------------------------------------------------------------------

// WiFi & MQTT credentials
static const char* kSSID       = "YOURWIFISSID";
static const char* kPassword   = "YOURWIFIPASSWORD";
static const char* kMQTTServer = "YOURHOSTIP";
static const int   kMQTTPort   = 1883;
static const char* kMQTTUser   = "YOURMQTTUSERNAME";
static const char* kMQTTPass   = "YOURMQTTUSERNAME";
static const char* kClientID   = "YOURMQTTPASSWORD";


WiFiClient wifiClient;            // WiFi client untuk koneksi TCP
PubSubClient mqtt(wifiClient);    // MQTT client menggunakan WiFiClient

// OLED SSD1306
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1
#define OLED_ADDRESS   0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// PZEM004Tv30 on Serial1 (RX=3, TX=2)
PZEM004Tv30 pzem(Serial1, 2, 3);

// MQTT topics
static const char* kStateTopics[6] = {
  "esp32/daya/volt",
  "esp32/daya/amp",
  "esp32/daya/power",
  "esp32/daya/energy",
  "esp32/daya/freq",
  "esp32/daya/pf"
};

static const char* kDiscoveryTopics[6] = {
  "homeassistant/sensor/esp32_panel_tegangan/config",
  "homeassistant/sensor/esp32_panel_arus/config",
  "homeassistant/sensor/esp32_panel_daya/config",
  "homeassistant/sensor/esp32_panel_energy/config",
  "homeassistant/sensor/esp32_panel_freq/config",
  "homeassistant/sensor/esp32_panel_pf/config"
};

static const char* kAvailabilityTopic = "esp32/status";

// Timers
unsigned long last_publish       = 0;
unsigned long last_display_switch= 0;
constexpr unsigned long kPublishInterval      = 30000;  // ms
constexpr unsigned long kDisplaySwitchInterval= 5000;   // ms

bool show_sensor_screen = false;

// Tambahkan variabel global untuk menyimpan data sensor terbaru:
float sensor_v = NAN;
float sensor_i = NAN;
float sensor_p = NAN;
float sensor_e = NAN;
float sensor_f = NAN;
float sensor_pf = NAN;

// Waktu interval
constexpr unsigned long kDisplayTotalInterval = 10000; // total 10 detik cycle
constexpr unsigned long kDisplayStatusDuration = 2000; // 2 detik status
constexpr unsigned long kDisplaySensorDuration = 8000; // 8 detik sensor


// Waktu terakhir update display dan tipe tampilan
unsigned long last_display_update = 0;
bool showing_status = true;  // true = status koneksi, false = sensor

// -----------------------------------------------------------------------------
// PROTOTYPES
// -----------------------------------------------------------------------------
void SetupWiFi();
void EnsureMQTTConnected();
void MQTTCallback(char* topic, byte* payload, unsigned int length);
void PublishDiscovery();
void PublishSensorData();
void PublishInitialData();
void UpdateDisplayStatus();
void UpdateDisplaySensor();
void EnsureWiFiConnected();

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
/**
 * @brief Arduino setup: init Serial, OLED, WiFi, MQTT, Discovery & first data.
 */
void setup() {
  Serial.begin(115200);
  delay(100);

  // Init OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("SSD1306 init failed"));
    while (true);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Connect WiFi & show IP
  SetupWiFi();
  display.clearDisplay();
  display.setCursor(0,0);
  display.println("WiFi Connected:");
  display.println(WiFi.localIP());
  display.display();

  // Setup MQTT
  mqtt.setServer(kMQTTServer, kMQTTPort);
  mqtt.setCallback(MQTTCallback);
  EnsureMQTTConnected();

  // Publish Home Assistant Discovery & availability
  PublishDiscovery();
  mqtt.publish(kAvailabilityTopic, "online", true);

  // Publish initial sensor data immediately
  PublishSensorData();
}

// -----------------------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------------------
/**
 * @brief Arduino main loop: maintain connections, publish & update display.
 */
void loop() {
  mqtt.loop();
  EnsureWiFiConnected();
  EnsureMQTTConnected();

  unsigned long now = millis();

  // Publish sensor setiap 30 detik (sesuai kamu)
  if (now - last_publish >= kPublishInterval) {
    last_publish = now;
    PublishSensorData();
  }

  // Update display berdasarkan timer dan interval masing-masing
  if (now - last_display_update >= (showing_status ? kDisplayStatusDuration : kDisplaySensorDuration)) {
    last_display_update = now;
    showing_status = !showing_status;  // toggle tampilan

    if (showing_status) {
      UpdateDisplayStatus();
    } else {
      UpdateDisplaySensor();
    }
  }
}

// -----------------------------------------------------------------------------
// WIFI HANDLING
// -----------------------------------------------------------------------------
/**
 * @brief Connect to WiFi (blocking until connected).
 */
void SetupWiFi() {
  WiFi.begin(kSSID, kPassword);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println(" connected");
}

/**
 * @brief Ensure WiFi stays connected; reconnect if dropped.
 */
void EnsureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.reconnect();
  }
}

// -----------------------------------------------------------------------------
// MQTT HANDLING
// -----------------------------------------------------------------------------
/**
 * @brief Reconnect to MQTT broker if disconnected (non-blocking).
 */
void EnsureMQTTConnected() {
  static unsigned long last_attempt = 0;
  if (mqtt.connected()) return;

  unsigned long now = millis();
  if (now - last_attempt < 5000) return;
  last_attempt = now;

  Serial.print("MQTT connecting...");
  // will message: publish "offline" on availabilityTopic if disconnect
  if (mqtt.connect(kClientID, kMQTTUser, kMQTTPass,
                   kAvailabilityTopic, 0, true, "offline")) {
    Serial.println(" connected");
    // re-publish discovery & initial data on reconnect
    PublishDiscovery();
    PublishInitialData();
  } else {
    Serial.printf(" failed rc=%d\n", mqtt.state());
  }
}

/**
 * @brief Dummy callback for incoming MQTT (unused).
 */
void MQTTCallback(char* topic, byte* payload, unsigned int length) {
  // No subscriptions in this app
}

// -----------------------------------------------------------------------------
// HOME ASSISTANT DISCOVERY
// -----------------------------------------------------------------------------
/**
 * @brief Publish MQTT Discovery payloads for all 6 sensors.
 */
void PublishDiscovery() {
  static const char* names[6]  = { "ESP32 Panel IoT Tegangan", "ESP32 Panel IoT Arus", "ESP32 Panel IoT Daya", "ESP32 Panel IoT Energy", "ESP32 Panel IoT Frequency", "ESP32 Panel IoT PowerFactor" };
  static const char* units[6]  = { "V", "A", "W", "kWh", "Hz", "" };
  static const char* classes[6]= { "tegangan","arus","daya","energy","frequency","power_factor" };

  for (int i=0; i<6; ++i) {
    StaticJsonDocument<256> doc;
    uint8_t buf[256];

    doc["name"]               = names[i];
    doc["state_topic"]        = kStateTopics[i];
    doc["availability_topic"] = kAvailabilityTopic;
    doc["unique_id"]          = String(kClientID) + "_" + classes[i];
    //doc["device_class"]       = classes[i];
    doc["unit_of_measurement"]= units[i];
    doc["state_class"]        = (i==3 ? "total_increasing" : "measurement");

    JsonObject device = doc.createNestedObject("device");
    device["identifiers"]  = "esp32_panel_iot_01";
    device["name"]         = "ESP32 Panel IoT";
    //device["model"]        = "ESP32-C3 Dummy Sensor";
    //device["manufacturer"] = "DLCB";

    size_t n = serializeJson(doc, (char*)buf, sizeof(buf));
    mqtt.publish(kDiscoveryTopics[i], buf, n, true);
  }
}

// -----------------------------------------------------------------------------
// SENSOR PUBLISH
// -----------------------------------------------------------------------------
/**
 * @brief Publish sensor data to all 6 state topics.
 */
void PublishSensorData() {
  float v = pzem.voltage();
  float i = pzem.current();
  float p = pzem.power();
  float e = pzem.energy();
  float f = pzem.frequency();
  float pf= pzem.pf();

  if (isnan(v) || isnan(i) || isnan(p)) {
    Serial.println("[ERROR] Invalid PZEM reading, skip publish");
    return;
  }

  // Simpan ke variabel global supaya OLED pakai ini
  sensor_v = v;
  sensor_i = i;
  sensor_p = p;
  sensor_e = e;
  sensor_f = f;
  sensor_pf= pf;

  const char* keys[6] = { "voltage", "current", "power", "energy", "frequency", "power_factor" };

  float vals[6] = { v,i,p,e,f,pf };

  for (int idx=0; idx<6; ++idx) {
    StaticJsonDocument<64> doc;
    char buf[64];
    doc[keys[idx]] = vals[idx];
    size_t n = serializeJson(doc, buf);
    mqtt.publish(kStateTopics[idx], (const uint8_t*)buf, n, true);
  }

  // Print to Serial for debugging
  Serial.printf(
    "Sensor Data -> Voltage: %.2f V, Current: %.2f A, Power: %.2f W, Energy: %.2f kWh, Frequency: %.2f Hz, PowerFactor: %.2f\n",
    v,i,p,e,f,pf 
  );

}

/**
 * @brief Publish initial sensor data & availability.
 */
void PublishInitialData() {
  mqtt.publish(kAvailabilityTopic, "online", true);
  PublishSensorData();
}

// -----------------------------------------------------------------------------
// OLED DISPLAY
// -----------------------------------------------------------------------------
/**
 * @brief Show WiFi/MQTT status & IP on OLED.
 */
void UpdateDisplayStatus() {
  display.clearDisplay();
  display.setCursor(0,10);
  display.println("Status Koneksi:");
  display.printf("WiFi: %s\n",   WiFi.status()==WL_CONNECTED?"OK":"FAIL");
  display.printf("MQTT: %s\n",  mqtt.connected()?"OK":"FAIL");
  display.println(String("IP: ")+WiFi.localIP().toString());
  display.display();
}

/**
 * @brief Show PZEM sensor data on OLED.
 */
void UpdateDisplaySensor() {
  display.clearDisplay();
  display.setCursor(0,10);
  if (isnan(sensor_v) || isnan(sensor_i) || isnan(sensor_p)) {
    display.println("Sensor Error!");
  } else {
    display.printf("V: %.1f V\n", sensor_v);
    display.printf("I: %.2f A\n", sensor_i);
    display.printf("P: %.1f W\n", sensor_p);
    display.printf("E: %.2f kWh\n", sensor_e);
    display.printf("F: %.1f Hz\n", sensor_f);
    display.printf("PF: %.2f\n", sensor_pf);
  }
  display.display();
}
