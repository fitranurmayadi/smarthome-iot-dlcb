/**
 * @file main.ino
 * @brief ESP32-S3 MQTT Integration with Home Assistant
 * @details Mengendalikan 8 perangkat (5 lampu, 1 kipas, 1 buzzer, 1 solenoid) dan membaca sensor DHT22,
 *          terintegrasi dua arah dengan Home Assistant via MQTT.
 *
 * @author 
 * @date 2025
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

#include "DHT.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// ========================== Konfigurasi ===========================
#define DHTPIN 14      ///< Pin untuk sensor DHT22
#define DHTTYPE DHT22  ///< Tipe sensor DHT
DHT dht(DHTPIN, DHTTYPE);

const int pins[] = { 4, 5, 6, 7, 15, 16, 17, 18 };  // 0-4: Lampu, 5: Kipas, 6: Buzzer, 7: Solenoid

const char* ssid 		= "YOURWIFISSID";
const char* password 		= "YOURWIFIPASSWORD";
const char* mqtt_server		= "YOURHOSTIP";
const int mqtt_port 		= 1883;
const char* mqtt_user 		= "YOURMQTTUSERNAME";
const char* mqtt_password 	= "YOURMQTTPASSWORD";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ========================== Fungsi Prototipe ======================
void reconnect();
void publishDiscovery();
void mqttCallback(char* topic, byte* payload, unsigned int length);


float suhuTerakhir = 0, kelembabanTerakhir = 0;
bool relayStates[8] = { false };  // Update di mqttCallback()


const unsigned char icon_clock[] PROGMEM = {
  0x00, 0x00, 0x3C, 0x00, 0x42, 0x00, 0x81, 0x00,
  0xA5, 0x00, 0x99, 0x00, 0x81, 0x00, 0x81, 0x00,
  0x81, 0x00, 0x99, 0x00, 0xA5, 0x00, 0x81, 0x00,
  0x42, 0x00, 0x3C, 0x00, 0x00, 0x00
};

const unsigned char icon_temp[] PROGMEM = {
  0x01, 0xC0, 0x03, 0xE0, 0x07, 0x20, 0x07, 0xE0,
  0x07, 0x20, 0x07, 0xE0, 0x07, 0x20, 0x07, 0xE0,
  0x07, 0x20, 0x0F, 0xF0, 0x1F, 0xF8, 0x1F, 0xF8,
  0x1F, 0xF8, 0x1F, 0xF8, 0x0F, 0xF0, 0x07, 0xE0
};

const unsigned char icon_humidity[] PROGMEM = {
  0x00, 0x00, 0x01, 0x80, 0x03, 0xC0, 0x07, 0xE0,
  0x0F, 0xF0, 0x0F, 0xF0, 0x1F, 0xF8, 0x1F, 0xD8,
  0x3F, 0x9C, 0x3F, 0x9C, 0x3F, 0x1C, 0x1E, 0x38,
  0x1F, 0xF8, 0x0F, 0xF0, 0x03, 0xC0, 0x00, 0x00
};

const unsigned char icon_wifi[] PROGMEM = {
  0x00, 0x1C, 0x22, 0x49, 0x85, 0x01, 0x00, 0x00,
  0x00, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00
};

const unsigned char icon_mqtt[] PROGMEM = {
  0x3C, 0x42, 0xA5, 0x81, 0x81, 0xA5, 0x42, 0x3C
};



// ========================== Setup Awal ============================
void setup() {
  Serial.begin(115200);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED gagal ditemukan"));
    while (true)
      ;  // Gagal inisialisasi
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // Inisialisasi semua pin sebagai OUTPUT dan default LOW
  for (int i = 0; i < 8; i++) {
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], HIGH);
  }
  dht.begin();

  // Koneksi WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  setupTime();
  // Konfigurasi MQTT
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  if (!isnan(temp) && !isnan(hum)) {
    char buf[64];
    sprintf(buf, "{\"temperature\":%.1f}", temp);
    mqtt.publish("esp32/dht22/suhu", buf, true);
    sprintf(buf, "{\"humidity\":%.1f}", hum);
    mqtt.publish("esp32/dht22/kelembaban", buf, true);
    suhuTerakhir = temp;
    kelembabanTerakhir = hum;
  }
}

// ========================== Loop Utama ============================
unsigned long lastPublish = 0;
void loop() {
  if (!mqtt.connected()) reconnect();
  mqtt.loop();
  updateDisplay();
  // Kirim data DHT22 tiap 60 detik
  if (millis() - lastPublish > 60000) {
    lastPublish = millis();
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    if (!isnan(temp) && !isnan(hum)) {
      char buf[64];
      sprintf(buf, "{\"temperature\":%.1f}", temp);
      mqtt.publish("esp32/dht22/suhu", buf, true);
      sprintf(buf, "{\"humidity\":%.1f}", hum);
      mqtt.publish("esp32/dht22/kelembaban", buf, true);
      suhuTerakhir = temp;
      kelembabanTerakhir = hum;
    }
  }
}

unsigned long lastDisplayChange = 0;
int displayPage = 0;
const int totalPages = 5;

void updateDisplay() {
  if (millis() - lastDisplayChange > 5000) {  // Ganti setiap 5 detik
    displayPage = (displayPage + 1) % totalPages;
    lastDisplayChange = millis();
  }

  display.clearDisplay();
  switch (displayPage) {
    case 0: showTime(); break;
    case 1: showTemperature(); break;
    case 2: showHumidity(); break;
    case 3: showRelayStatus(); break;
    case 4: showNetworkStatus(); break;
  }
  display.display();
}



void setupTime() {
  configTime(7 * 3600, 0, "pool.ntp.org");  // UTC+7
}

// SCREEN NUMBER X: TIME & DATE
void showTime() {
  struct tm timeinfo;
  display.clearDisplay();

  if (getLocalTime(&timeinfo)) {
    display.setTextSize(3);
    display.setCursor(19, 5);
    display.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);  // jam dan menit saja

    display.setTextSize(1);
    display.setCursor(16, 40);
    char dateStr[20];
    strftime(dateStr, sizeof(dateStr), "%a, %d %b %Y", &timeinfo);
    display.print(dateStr);
  } else {
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("Waktu tidak sinkron");
  }


  display.display();
}

// SCREEN NUMBER X: TEMPERATURE
void showTemperature() {
  display.clearDisplay();
  display.setTextSize(2);
  display.drawBitmap(15, 5, icon_temp, 16, 16, 1);
  display.setCursor(35, 5);
  display.print(suhuTerakhir);
  display.cp437(true);
  display.setTextSize(1);
  display.print(" ");
  display.write(167);
  display.print("C");
  display.setCursor(0, 34);
  display.setTextSize(1);
  display.print("Temperature: ");
  display.print(suhuTerakhir);
  display.cp437(true);
  display.print(" ");
  display.write(167);
  display.print("C");
  display.setCursor(0, 44);
  display.setTextSize(1);
  display.print("Humidity: ");
  display.print(kelembabanTerakhir);
  display.print(" %");
  display.display();
}

// SCREEN NUMBER X: HUMIDITY
void showHumidity() {
  display.clearDisplay();
  display.setTextSize(2);
  display.drawBitmap(15, 5, icon_humidity, 16, 16, 1);
  display.setCursor(35, 5);
  display.print(kelembabanTerakhir);
  display.print(" %");
  display.setCursor(0, 34);
  display.setTextSize(1);
  display.print("Humidity: ");
  display.print(kelembabanTerakhir);
  display.print(" %");
  display.setCursor(0, 44);
  display.setTextSize(1);
  display.print("Temperature: ");
  display.print(suhuTerakhir);
  display.cp437(true);
  display.print(" ");
  display.write(167);
  display.print("C");
  display.display();
}

void showRelayStatus() {
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Relay Status:");
  for (int i = 0; i < 8; i++) {
    display.printf("R%d:%s  ", i + 1, relayStates[i] ? "ON " : "OFF");
    if (i == 1 || i == 3 || i == 5 || i == 7) display.println();
  }
  display.display();
}

// SCREEN NUMBER X: NETWORK STATUS
void showNetworkStatus() {
  display.clearDisplay();
  display.setTextSize(1.5);
  display.setCursor(5, 0);
  display.printf("WiFi:  %s", WiFi.status() == WL_CONNECTED ? "OK" : "ERR");
  display.setCursor(5, 25);
  display.printf("MQTT:  %s", mqtt.connected() ? "OK" : "ERR");
  display.setCursor(5, 50);
  display.print("IP: ");
  display.print(WiFi.localIP());
  display.display();
}
// ========================== Koneksi Ulang =========================
void reconnect() {
  while (!mqtt.connected()) {
    if (mqtt.connect("ESP32S3Panel", mqtt_user, mqtt_password)) {
      Serial.println("MQTT connected");
      for (int i = 0; i < 5; i++) mqtt.subscribe(("esp32/relay/" + String(i) + "/set").c_str());
      mqtt.subscribe("esp32/fan/set");
      mqtt.subscribe("esp32/buzzer/set");
      mqtt.subscribe("esp32/solenoid/set");

      publishDiscovery();

      for (int i = 0; i < 5; i++) mqtt.publish(("esp32/relay/" + String(i) + "/available").c_str(), "online", true);
      mqtt.publish("esp32/fan/available", "online", true);
      mqtt.publish("esp32/buzzer/available", "online", true);
      mqtt.publish("esp32/solenoid/available", "online", true);
    } else {
      delay(5000);
    }
  }
}

// ========================== Callback MQTT =========================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String msg = String((char*)payload).substring(0, length);
  Serial.printf("[MQTT] Topic: %s, Message: %s\n", topic, msg.c_str());

  bool on = (msg == "ON");

  // Lampu 0-4
  for (int i = 0; i < 5; i++) {
    if (t == "esp32/relay/" + String(i) + "/set") {
      digitalWrite(pins[i], on ? LOW : HIGH);
      relayStates[i] = on;
      mqtt.publish(("esp32/relay/" + String(i) + "/state").c_str(), on ? "ON" : "OFF", true);
      return;
    }
  }

  // Perangkat lainnya
  struct {
    const char* topic;
    int index;
    const char* state;
  } map[] = {
    { "esp32/fan/set", 5, "esp32/fan/state" },
    { "esp32/buzzer/set", 6, "esp32/buzzer/state" },
    { "esp32/solenoid/set", 7, "esp32/solenoid/state" }
  };
  for (auto& m : map) {
    if (t == m.topic) {
      digitalWrite(pins[m.index], on ? LOW : HIGH);
      mqtt.publish(m.state, on ? "ON" : "OFF", true);
      return;
    }
  }
}

// ========================== MQTT Discovery ========================
void publishDiscovery() {
  for (int i = 0; i < 5; i++) {
    String topic = "homeassistant/switch/esp32s3_lampu_" + String(i) + "/config";
    String payload = "{\"name\":\"Lampu " + String(i) + "\",\"command_topic\":\"esp32/relay/" + String(i) + "/set\"," + "\"state_topic\":\"esp32/relay/" + String(i) + "/state\"," + "\"availability_topic\":\"esp32/relay/" + String(i) + "/available\"," + "\"payload_on\":\"ON\",\"payload_off\":\"OFF\"," + "\"unique_id\":\"esp32s3_lampu_" + String(i) + "\"," + "\"device\":{\"identifiers\":[\"esp32s3_panel\"],\"name\":\"ESP32-S3 Panel\"}}";
    mqtt.publish(topic.c_str(), payload.c_str(), true);
  }

  const char* names[] = { "Kipas", "Alarm Buzzer", "Solenoid Lock" };
  const char* cmd[] = { "esp32/fan/set", "esp32/buzzer/set", "esp32/solenoid/set" };
  const char* state[] = { "esp32/fan/state", "esp32/buzzer/state", "esp32/solenoid/state" };
  const char* avail[] = { "esp32/fan/available", "esp32/buzzer/available", "esp32/solenoid/available" };

  for (int i = 0; i < 3; i++) {
    String topic = "homeassistant/switch/esp32s3_dev" + String(i) + "/config";
    String payload = "{\"name\":\"" + String(names[i]) + "\",\"command_topic\":\"" + String(cmd[i]) + "\"," + "\"state_topic\":\"" + String(state[i]) + "\"," + "\"availability_topic\":\"" + String(avail[i]) + "\"," + "\"payload_on\":\"ON\",\"payload_off\":\"OFF\"," + "\"unique_id\":\"esp32s3_dev_" + String(i) + "\"," + "\"device\":{\"identifiers\":[\"esp32s3_panel\"],\"name\":\"ESP32-S3 Panel\"}}";
    mqtt.publish(topic.c_str(), payload.c_str(), true);
  }

  const char* dhtNames[] = { "Suhu DHT22", "Kelembaban DHT22" };
  const char* dhtTopics[] = { "esp32/dht22/suhu", "esp32/dht22/kelembaban" };
  const char* dhtUnits[] = { "°C", "%" };
  const char* templates[] = { "{{ value_json.temperature }}", "{{ value_json.humidity }}" };

  for (int i = 0; i < 2; i++) {
    String topic = "homeassistant/sensor/esp32s3_dht" + String(i) + "/config";
    String payload = "{\"name\":\"" + String(dhtNames[i]) + "\",\"state_topic\":\"" + String(dhtTopics[i]) + "\"," + "\"unit_of_measurement\":\"" + String(dhtUnits[i]) + "\"," + "\"value_template\":\"" + String(templates[i]) + "\"," + "\"unique_id\":\"esp32s3_dht_" + String(i) + "\"," + "\"device\":{\"identifiers\":[\"esp32s3_panel\"],\"name\":\"ESP32-S3 Panel\"}}";
    mqtt.publish(topic.c_str(), payload.c_str(), true);
  }
}
