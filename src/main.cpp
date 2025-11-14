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
#include "mqtt.h"

// ===== GLOBAL OBJECTS =====
AsyncWebServer server(80);
// WiFiClient and mqttClient are now defined in src/mqtt.cpp
Preferences preferences;
WiFiManager wifiManager;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

Config config;
IOPin ioPins[MAX_IOS];
AccessLog accessLogs[100];   // Max 100 logs
int ioPinCount = 0;

unsigned long lastMqttReconnect = 0;

// Bouton pour reset WiFi (bouton BOOT sur ESP32)
#define RESET_WIFI_BUTTON 0
#define STATUS_LED 23

// ===== PROTOTYPES =====
void loadConfig();
void saveConfig();
void loadIOs();
void saveIOs();
void applyIOPinModes();
void handleIOs();
void setupWebServer();
// MQTT functions were moved to src/mqtt.cpp and declared in mqtt.h
void setupNTP();
void blinkStatusLED(int times, int delayMs);


// ===== FONCTION RESET WiFi =====
// Fonction pour détecter 3 appuis sur le bouton BOOT
bool checkTriplePress() {
  int pressCount = 0;
  unsigned long startTime = millis();
  unsigned long lastPressTime = 0;
  bool lastState = HIGH;
  
  Serial.println("\n⏱ WiFi Reset Check (5 seconds window)...");
  Serial.println("Press BOOT button 3 times to reset WiFi credentials");
  
  while (millis() - startTime < 5000) {  // 5 secondes
    bool currentState = digitalRead(RESET_WIFI_BUTTON);
    
    // Détection front descendant (appui)
    if (lastState == HIGH && currentState == LOW) {
      pressCount++;
      lastPressTime = millis();
      Serial.printf("✓ Press %d/3 detected\n", pressCount);
      
      if (pressCount >= 3) {
        Serial.println("\n🔥 Triple press detected!");
        return true;
      }
      
      delay(50);  // Anti-rebond
    }
    
    lastState = currentState;
    delay(10);
  }
  
  if (pressCount > 0) {
    Serial.printf("Only %d press(es) detected. Reset cancelled.\n", pressCount);
  }
  Serial.println("No reset requested. Continuing...\n");
  return false;
}


// ===== SETUP =====

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== ESP32 Generic IO Controller ===");
  Serial.println("Version 1.0");
  Serial.println("Chip ID: " + String((uint32_t)ESP.getEfuseMac(), HEX));
  Serial.println("SDK Version: " + String(ESP.getSdkVersion()));

  blinkStatusLED(3, 200);

  // Load configuration from flash
  preferences.begin("generic-io", false);
  loadConfig();
  loadIOs();
  Serial.println("Configuration and I/O settings loaded.");
  blinkStatusLED(2, 100);
  // Apply I/O pin configurations
  applyIOPinModes();
  Serial.println("I/O pin configurations applied.");
  blinkStatusLED(2, 100);

  // ===== CONFIGURATION WiFi EN PREMIER =====
  // Configuration WiFiManager (AVANT les paramètres WiFi)
  wifiManager.setConfigPortalTimeout(180);  // 3 minutes pour configurer
  wifiManager.setConnectTimeout(30);        // 30 secondes pour se connecter
  wifiManager.setConnectRetries(3);         // 3 tentatives de connexion
  wifiManager.setDebugOutput(true);         // Activer le debug
  
  // Vérifier triple appui pour reset WiFi
  if (checkTriplePress()) {
    Serial.println("\n⚠⚠⚠ RESETTING WiFi credentials ⚠⚠⚠");
    wifiManager.resetSettings();
    delay(1000);
    Serial.println("Credentials erased. Restarting...");
    delay(2000);
    ESP.restart();
  }
  
  // Tentative de connexion WiFi
  Serial.println("\n⏱ Starting WiFi configuration...");
  Serial.println("If no saved credentials, access point will start:");
  Serial.println("SSID: ESP32-Roller-Setup");
  Serial.println("No password required");
  Serial.println("Connect and configure WiFi at: http://192.168.4.1\n");
  
  // Configuration WiFi pour compatibilité Freebox (juste avant autoConnect)
 // WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Réduire la puissance pour éviter les timeouts
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  // Configure une IP statique si le DHCP est désactivé sur le routeur/switch
  // ADAPTE ces valeurs : localIP doit être unique dans le réseau
  IPAddress localIP(192, 168, 1, 80);   // <-- choisir une IP libre
  IPAddress gateway(192, 168, 1, 1);   // <-- gateway fournie
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns1(8, 8, 8, 8);
  if (WiFi.config(localIP, gateway, subnet, dns1) == false) {
    Serial.println("⚠️ WiFi.config échoué");
  } else {
    Serial.print("✓ Static IP configured: ");
    Serial.println(localIP);
  }
  
  digitalWrite(STATUS_LED, HIGH);
  
  
  if (!wifiManager.autoConnect("ESP32-WifiMQTTRelay-Setup")) {
    Serial.println("\n✗✗✗ WiFiManager failed to connect ✗✗✗");
    Serial.println("Restarting in 5 seconds...");
    digitalWrite(STATUS_LED, LOW);
    delay(5000);
    ESP.restart();
  }
  
  // Connexion réussie
  Serial.println("\n✓✓✓ WiFi CONNECTED ✓✓✓");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  digitalWrite(STATUS_LED, LOW);
  
  // Arrêter le serveur de configuration WiFiManager pour libérer le port 80
  wifiManager.stopConfigPortal();
  delay(500);  // Attendre la libération du port

   // Démarrage du serveur
  server.begin();
  Serial.println("✓ Web server started");
  Serial.println("\n========================================");
  Serial.println("Access the web interface at:");
  Serial.print("http://");
  Serial.println(WiFi.localIP());
  Serial.println("========================================\n");

  // Arrêter le serveur de configuration WiFiManager pour libérer le port 80
  wifiManager.stopConfigPortal();
  delay(500);  // Attendre la libération du port
  Serial.println("✓ Config portal stopped to free port 80");

  // Setup Web Server
  setupWebServer();

  // Initialize LittleFS
  //if(!LittleFS.begin(true)){
  //  Serial.println("An Error has occurred while mounting LittleFS");
  //  return;
  //}
  //Serial.println("LittleFS mounted successfully.");

  // Setup NTP
  //setupNTP();

  // Setup MQTT
  setupMQTT();

  server.begin();
  Serial.println("Web server started and configured.");
  Serial.println("========================================");
}


// ===== LOOP =====
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    // Only run MQTT processing if the subsystem is explicitly enabled
    if (mqttEnabled) {
      if (!mqttClient.connected()) {
        long now = millis();
        if (now - lastMqttReconnect > 5000) {
          lastMqttReconnect = now;
          reconnectMQTT();
        }
      }
      mqttClient.loop();
    }
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
            if (mqttEnabled) publishMQTT(topic, currentState ? "1" : "0");
          }
        }
      }
    }
  }
}

// ===== MQTT FUNCTIONS =====
// NOTE: MQTT implementation moved to src/mqtt.cpp
// The original implementation has been removed from this file to avoid
// duplicate symbols. See src/mqtt.cpp and include "mqtt.h" for the API.

void blinkStatusLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(delayMs);
    digitalWrite(STATUS_LED, LOW);
    delay(delayMs);
  }
}
