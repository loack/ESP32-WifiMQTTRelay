#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <LittleFS.h>

#include "config.h"

// ===== GLOBAL OBJECTS =====
AsyncWebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Preferences preferences;
WiFiManager wifiManager;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

Config config;
IOPin ioPins[MAX_IOS];
AccessLog accessLogs[100];   // Max 100 logs
int ioPinCount = 0;

unsigned long lastMqttReconnect = 0;


// ===== PROTOTYPES =====
void loadConfig();
void saveConfig();
void loadIOs();
void saveIOs();
void applyIOPinModes();
void handleIOs();
void setupWebServer();
void setupMQTT();
void reconnectMQTT();
void publishMQTT(const char* topic, const char* payload);
void setupNTP();

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== ESP32 Generic IO Controller ===");
  Serial.println("Version 1.0");

  // Load configuration from flash
  preferences.begin("generic-io", false);
  loadConfig();
  loadIOs();

  // Setup WiFi
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(30);
  if (!wifiManager.autoConnect("ESP32-IO-Setup")) {
    Serial.println("Failed to connect to WiFi, restarting...");
    delay(5000);
    ESP.restart();
  }
  Serial.println("WiFi connected.");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Apply I/O pin configurations
  applyIOPinModes();

  // Initialize LittleFS
  if(!LittleFS.begin(true)){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully.");

  // Setup NTP
  setupNTP();

  // Setup Web Server
  setupWebServer();

  // Setup MQTT
  setupMQTT();

  // Start ElegantOTA
  ElegantOTA.begin(&server);

  server.begin();
  Serial.println("Web server started.");
  Serial.println("========================================");
}

// ===== LOOP =====
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      long now = millis();
      if (now - lastMqttReconnect > 5000) {
        lastMqttReconnect = now;
        reconnectMQTT();
      }
    }
    mqttClient.loop();
  }

  timeClient.update();
  handleIOs();
  ElegantOTA.loop();
  delay(10);
}

// ===== CONFIGURATION FUNCTIONS =====
void loadConfig() {
  preferences.getString("adminPw", config.adminPassword, sizeof(config.adminPassword));
  if (strlen(config.adminPassword) == 0) strcpy(config.adminPassword, "admin");

  preferences.getString("mqttSrv", config.mqttServer, sizeof(config.mqttServer));
  config.mqttPort = preferences.getInt("mqttPort", 1883);
  preferences.getString("mqttUser", config.mqttUser, sizeof(config.mqttUser));
  preferences.getString("mqttPass", config.mqttPassword, sizeof(config.mqttPassword));
  preferences.getString("mqttTop", config.mqttTopic, sizeof(config.mqttTopic));
  if (strlen(config.mqttTopic) == 0) strcpy(config.mqttTopic, "esp32/io");

  preferences.getString("ntpSrv", config.ntpServer, sizeof(config.ntpServer));
  if (strlen(config.ntpServer) == 0) strcpy(config.ntpServer, "pool.ntp.org");
  config.gmtOffset_sec = preferences.getLong("gmtOffset", 3600);
  config.daylightOffset_sec = preferences.getInt("daylightOff", 3600);

  config.initialized = preferences.getBool("init", false);
  Serial.println("Configuration loaded.");
}

void saveConfig() {
  preferences.putString("adminPw", config.adminPassword);
  preferences.putString("mqttSrv", config.mqttServer);
  preferences.putInt("mqttPort", config.mqttPort);
  preferences.putString("mqttUser", config.mqttUser);
  preferences.putString("mqttPass", config.mqttPassword);
  preferences.putString("mqttTop", config.mqttTopic);
  preferences.putString("ntpSrv", config.ntpServer);
  preferences.putLong("gmtOffset", config.gmtOffset_sec);
  preferences.putInt("daylightOff", config.daylightOffset_sec);
  preferences.putBool("init", true);
  Serial.println("Configuration saved.");
}

void loadIOs() {
  ioPinCount = preferences.getInt("ioCount", 0);
  if (ioPinCount > MAX_IOS) ioPinCount = 0;
  for (int i = 0; i < ioPinCount; i++) {
    String key = "io" + String(i);
    preferences.getBytes(key.c_str(), &ioPins[i], sizeof(IOPin));
  }
  Serial.printf("Loaded %d I/O pin configurations.\n", ioPinCount);
}

void saveIOs() {
  preferences.putInt("ioCount", ioPinCount);
  for (int i = 0; i < ioPinCount; i++) {
    String key = "io" + String(i);
    preferences.putBytes(key.c_str(), &ioPins[i], sizeof(IOPin));
  }
  Serial.printf("Saved %d I/O pin configurations.\n", ioPinCount);
}

void applyIOPinModes() {
    for (int i = 0; i < ioPinCount; i++) {
        if (ioPins[i].mode == 1) { // INPUT
            pinMode(ioPins[i].pin, INPUT_PULLUP); // Or INPUT, depending on needs
        } else if (ioPins[i].mode == 2) { // OUTPUT
            pinMode(ioPins[i].pin, OUTPUT);
            digitalWrite(ioPins[i].pin, ioPins[i].defaultState);
        }
    }
    Serial.println("I/O pin modes applied.");
}


// ===== NTP FUNCTIONS =====
void setupNTP() {
  timeClient.begin();
  timeClient.setTimeOffset(config.gmtOffset_sec);
  timeClient.setUpdateInterval(60000); // Update every minute
  timeClient.forceUpdate();
  Serial.println("NTP client setup.");
  Serial.print("Current time: ");
  Serial.println(timeClient.getFormattedTime());
}

// ===== I/O HANDLING =====
void handleIOs() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  // Check inputs every 50ms for changes
  if (now - lastCheck > 50) {
    lastCheck = now;
    for (int i = 0; i < ioPinCount; i++) {
      if (ioPins[i].mode == 1) { // INPUT
        bool currentState = digitalRead(ioPins[i].pin);
        if (currentState != ioPins[i].state) {
          // Simple debounce: wait for a second read to confirm
          delay(10); 
          if (digitalRead(ioPins[i].pin) == currentState) {
            ioPins[i].state = currentState;
            Serial.printf("Input '%s' (pin %d) changed to %s\n", ioPins[i].name, ioPins[i].pin, currentState ? "HIGH" : "LOW");
            
            char topic[128];
            snprintf(topic, sizeof(topic), "status/%s", ioPins[i].name);
            publishMQTT(topic, currentState ? "1" : "0");
          }
        }
      }
    }
  }
}

// ===== MQTT FUNCTIONS =====
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    Serial.printf("MQTT message arrived [%s] ", topic);
    
    // Convert payload to string
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';
    Serial.println(message);

    // Topic structure is expected to be: <base_topic>/control/<pin_name>/set
    String topicStr = String(topic);
    String baseTopic = String(config.mqttTopic);

    if (!topicStr.startsWith(baseTopic) || !topicStr.endsWith("/set")) {
        return; // Not a command for us
    }

    // Extract pin name
    String pinName = topicStr.substring(baseTopic.length() + 9, topicStr.length() - 4);

    // Find the IO pin by name
    for (int i = 0; i < ioPinCount; i++) {
        if (String(ioPins[i].name) == pinName) {
            if (ioPins[i].mode == 2) { // OUTPUT
                bool newState = (strcmp(message, "1") == 0 || strcmp(message, "ON") == 0 || strcmp(message, "true") == 0);
                
                digitalWrite(ioPins[i].pin, newState);
                ioPins[i].state = newState;

                Serial.printf("Output '%s' (pin %d) set to %s\n", ioPins[i].name, ioPins[i].pin, newState ? "HIGH" : "LOW");

                // Publish the new state to the status topic
                char statusTopic[128];
                snprintf(statusTopic, sizeof(statusTopic), "status/%s", ioPins[i].name);
                publishMQTT(statusTopic, newState ? "1" : "0");

            } else {
                Serial.printf("Received command for non-output pin '%s'\n", pinName.c_str());
            }
            return;
        }
    }

    Serial.printf("Received command for unknown pin '%s'\n", pinName.c_str());
}

void setupMQTT() {
  mqttClient.setServer(config.mqttServer, config.mqttPort);
  mqttClient.setCallback(mqtt_callback);
  Serial.println("MQTT setup.");
}

void reconnectMQTT() {
  Serial.print("Attempting MQTT connection...");
  String clientId = "ESP32-IO-Controller-";
  clientId += String(random(0xffff), HEX);
  if (mqttClient.connect(clientId.c_str(), config.mqttUser, config.mqttPassword)) {
    Serial.println("connected");
    
    // Subscribe to control topics
    String topic = String(config.mqttTopic) + "/control/#";
    mqttClient.subscribe(topic.c_str());
    Serial.printf("Subscribed to %s\n", topic.c_str());

    // Publish current state of all pins
    for (int i = 0; i < ioPinCount; i++) {
        char statusTopic[128];
        snprintf(statusTopic, sizeof(statusTopic), "status/%s", ioPins[i].name);
        publishMQTT(statusTopic, ioPins[i].state ? "1" : "0");
    }

  } else {
    Serial.print("failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" try again in 5 seconds");
  }
}

void publishMQTT(const char* sub_topic, const char* payload) {
    if (mqttClient.connected()) {
        char fullTopic[128];
        snprintf(fullTopic, sizeof(fullTopic), "%s/%s", config.mqttTopic, sub_topic);
        mqttClient.publish(fullTopic, payload);
    }
}
