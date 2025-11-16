#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <SPIFFS.h>
#include <time.h>

#include "config.h"
#include "mqtt.h"

// ===== GLOBAL OBJECTS =====
AsyncWebServer server(80);
// WiFiClient and mqttClient are now defined in src/mqtt.cpp
Preferences preferences;
WiFiManager wifiManager;

Config config;
IOPin ioPins[MAX_IOS];
AccessLog accessLogs[100];   // Max 100 logs
int ioPinCount = 0;

ScheduledCommand scheduledCommands[MAX_SCHEDULED_COMMANDS];

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
void handleIOs(void *pvParameters); // Modified for FreeRTOS
void setupWebServer();
void blinkStatusLED(int times, int delayMs);
void processScheduledCommands();

// ===== FreeRTOS Task Handles =====
TaskHandle_t ioTaskHandle = NULL;

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

  // Initialize scheduled commands queue
  for (int i = 0; i < MAX_SCHEDULED_COMMANDS; i++) {
    scheduledCommands[i].active = false;
  }

  Serial.println("\n\n=== ESP32 Generic IO Controller ===");
  Serial.println("Version 1.0");
  Serial.println("Chip ID: " + String((uint32_t)ESP.getEfuseMac(), HEX));
  Serial.println("SDK Version: " + String(ESP.getSdkVersion()));

  blinkStatusLED(3, 200);

  // Load configuration from flash
  preferences.begin("generic-io", false);
  
  // Check WiFi connection failure counter
  int wifiFailCount = preferences.getInt("wifiFailCount", 0);
  Serial.printf("WiFi failure count: %d/3\n", wifiFailCount);
  
  if (wifiFailCount >= 3) {
    Serial.println("\n⚠️⚠️⚠️ TOO MANY WiFi FAILURES ⚠️⚠️⚠️");
    Serial.println("Resetting WiFi credentials...");
    wifiManager.resetSettings();
    preferences.putInt("wifiFailCount", 0);
    delay(2000);
    Serial.println("WiFi reset complete. Restarting...");
    ESP.restart();
  }
  
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
  
  // Custom parameters for static IP
  char useStaticIP_char[2] = { config.useStaticIP ? 'T' : 'F', 0 };
  WiFiManagerParameter custom_use_static_ip("use_static_ip", "Use Static IP", useStaticIP_char, 2, "type='checkbox'");
  WiFiManagerParameter custom_static_ip("static_ip", "Static IP", config.staticIP, 40);
  WiFiManagerParameter custom_static_gateway("static_gateway", "Static Gateway", config.staticGateway, 40);
  WiFiManagerParameter custom_static_subnet("static_subnet", "Static Subnet", config.staticSubnet, 40);

  wifiManager.addParameter(&custom_use_static_ip);
  wifiManager.addParameter(&custom_static_ip);
  wifiManager.addParameter(&custom_static_gateway);
  wifiManager.addParameter(&custom_static_subnet);

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
  
  if (config.useStaticIP) {
    IPAddress localIP, gateway, subnet, dns1(8, 8, 8, 8);
    localIP.fromString(config.staticIP);
    gateway.fromString(config.staticGateway);
    subnet.fromString(config.staticSubnet);
    if (WiFi.config(localIP, gateway, subnet, dns1) == false) {
      Serial.println("⚠️ Static IP Configuration Failed");
    } else {
      Serial.print("✓ Static IP configured: ");
      Serial.println(localIP);
    }
  }
  
  // Faire clignoter la LED pendant la tentative de connexion
  blinkStatusLED(5, 100);
  
  if (!wifiManager.autoConnect((String(config.deviceName) + "-Setup").c_str())) {
    Serial.println("\n✗✗✗ WiFiManager failed to connect ✗✗✗");
    
    // Incrémenter le compteur d'échecs
    int failCount = preferences.getInt("wifiFailCount", 0);
    failCount++;
    preferences.putInt("wifiFailCount", failCount);
    Serial.printf("WiFi failure count incremented to: %d/3\n", failCount);
    
    Serial.println("Restarting in 5 seconds...");
    
    // Clignoter rapidement la LED pour indiquer l'échec
    blinkStatusLED(10, 250);
    
    ESP.restart();
  }
  
  // Save the custom parameters
  config.useStaticIP = (strcmp(custom_use_static_ip.getValue(), "T") == 0);
  strcpy(config.staticIP, custom_static_ip.getValue());
  strcpy(config.staticGateway, custom_static_gateway.getValue());
  strcpy(config.staticSubnet, custom_static_subnet.getValue());
  saveConfig();

  // Connexion réussie - réinitialiser le compteur d'échecs
  preferences.putInt("wifiFailCount", 0);
  blinkStatusLED(3, 100);  // Signal de succès
  Serial.println("\n✓✓✓ WiFi CONNECTED ✓✓✓");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // === OPTIMISATION LATENCE ===
  // Désactiver le mode économie d'énergie du WiFi pour réduire la latence du ping
  WiFi.setSleep(false);
  Serial.println("✓ WiFi power-saving mode disabled to reduce latency.");
  // ==========================

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

  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully.");

  // Setup MQTT
  setupMQTT();
  if (strlen(config.mqttServer) > 0) {
    Serial.println("MQTT configuration found, enabling MQTT.");
    mqttEnabled = true;
    blinkStatusLED(2, 100);  // Signal MQTT activé
  }

  // === DÉMARRAGE TÂCHE I/O ===
  // Crée la tâche pour gérer les I/O sur le coeur 0, avec une haute priorité
  xTaskCreatePinnedToCore(
      handleIOs,        // Fonction de la tâche
      "IOTask",         // Nom de la tâche
      4096,             // Taille de la pile
      NULL,             // Paramètres de la tâche
      1,                // Priorité
      &ioTaskHandle,    // Handle de la tâche
      0);               // Cœur 0

  server.begin();
  Serial.println("Web server started and configured.");
  Serial.println("========================================");
  blinkStatusLED(1, 500);  // Signal de démarrage complet
}


// ===== LOOP =====
void loop() {
  // The main loop is now responsible for high-frequency tasks only.
  // I/O handling is moved to a separate FreeRTOS task.

  processScheduledCommands();

  if (WiFi.status() == WL_CONNECTED) {
    if (mqttEnabled) {
      if (!mqttClient.connected()) {
        long now = millis();
        // Attempt to reconnect every 5 seconds if disconnected.
        if (now - lastMqttReconnect > 5000) {
          lastMqttReconnect = now;
          reconnectMQTT();
        }
      }
      // This should be called as often as possible.
      mqttClient.loop();
    }
  }

  // ElegantOTA loop for web updates.
  ElegantOTA.loop();

  // A small delay can be added here if needed to prevent watchdog timeouts,
  // but it should be as small as possible (e.g., 1ms) or removed entirely
  // if other tasks yield frequently enough.
  delay(1);
}

void processScheduledCommands() {
  // Obtenir le temps actuel avec précision microseconde
  struct timeval tv;
  gettimeofday(&tv, NULL);
  uint64_t currentTimeUs = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
  
  for (int i = 0; i < MAX_SCHEDULED_COMMANDS; i++) {
    if (scheduledCommands[i].active) {
      // Calculer le temps d'exécution prévu en microsecondes
      uint64_t execTimeUs = ((uint64_t)scheduledCommands[i].exec_at_sec * 1000000ULL) + 
                             (uint64_t)scheduledCommands[i].exec_at_us;
      
      // Vérifier si le moment d'exécution est arrivé
      if (currentTimeUs >= execTimeUs) {
        // Calculer le délai d'exécution (peut être négatif si en avance)
        int64_t delay_us = (int64_t)currentTimeUs - (int64_t)execTimeUs;
        
        // Exécuter la commande
        executeCommand(scheduledCommands[i].pin, scheduledCommands[i].state);
        
        // Désactiver cette commande
        scheduledCommands[i].active = false;
        
        // Afficher le délai en millisecondes avec 3 décimales
        double delay_ms = delay_us / 1000.0;
        Serial.printf("⏰ Scheduled command executed (delay: %.3f ms)\n", delay_ms);
      }
    }
  }
}

// ===== CONFIGURATION FUNCTIONS =====
void loadConfig() {
  preferences.getString("deviceName", config.deviceName, sizeof(config.deviceName));
  if (strlen(config.deviceName) == 0) strcpy(config.deviceName, "esp32");

  config.useStaticIP = preferences.getBool("useStaticIP", false);
  preferences.getString("staticIP", config.staticIP, sizeof(config.staticIP));
  preferences.getString("staticGW", config.staticGateway, sizeof(config.staticGateway));
  preferences.getString("staticSN", config.staticSubnet, sizeof(config.staticSubnet));

  preferences.getString("adminPw", config.adminPassword, sizeof(config.adminPassword));
  if (strlen(config.adminPassword) == 0) strcpy(config.adminPassword, "admin");

  preferences.getString("mqttSrv", config.mqttServer, sizeof(config.mqttServer));
  config.mqttPort = preferences.getInt("mqttPort", 1883);
  preferences.getString("mqttUser", config.mqttUser, sizeof(config.mqttUser));
  preferences.getString("mqttPass", config.mqttPassword, sizeof(config.mqttPassword));
  preferences.getString("mqttTop", config.mqttTopic, sizeof(config.mqttTopic));
  if (strlen(config.mqttTopic) == 0) {
    snprintf(config.mqttTopic, sizeof(config.mqttTopic), "%s/io", config.deviceName);
  }

  // NTP settings are now for display and offset, not for server connection
  config.gmtOffset_sec = preferences.getLong("gmtOffset", 3600);
  config.daylightOffset_sec = preferences.getInt("daylightOff", 3600);

  config.initialized = preferences.getBool("init", false);
  Serial.println("Configuration loaded.");
}

void saveConfig() {
  preferences.putString("deviceName", config.deviceName);
  preferences.putBool("useStaticIP", config.useStaticIP);
  preferences.putString("staticIP", config.staticIP);
  preferences.putString("staticGW", config.staticGateway);
  preferences.putString("staticSN", config.staticSubnet);
  
  preferences.putString("adminPw", config.adminPassword);
  preferences.putString("mqttSrv", config.mqttServer);
  preferences.putInt("mqttPort", config.mqttPort);
  preferences.putString("mqttUser", config.mqttUser);
  preferences.putString("mqttPass", config.mqttPassword);
  preferences.putString("mqttTop", config.mqttTopic);
  //preferences.putString("ntpSrv", config.ntpServer); // No longer needed
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

    pinMode(STATUS_LED, OUTPUT); // Définit GPIO 23 comme une sortie
    for (int i = 0; i < ioPinCount; i++) {
        if (ioPins[i].mode == 1) { // INPUT
            // Apply the selected input type
            switch (ioPins[i].inputType) {
                case 0:
                    pinMode(ioPins[i].pin, INPUT);
                    Serial.printf("Pin %d (%s) configured as INPUT\n", ioPins[i].pin, ioPins[i].name);
                    break;
                case 1:
                    pinMode(ioPins[i].pin, INPUT_PULLUP);
                    Serial.printf("Pin %d (%s) configured as INPUT_PULLUP\n", ioPins[i].pin, ioPins[i].name);
                    break;
                case 2:
                    pinMode(ioPins[i].pin, INPUT_PULLDOWN);
                    Serial.printf("Pin %d (%s) configured as INPUT_PULLDOWN\n", ioPins[i].pin, ioPins[i].name);
                    break;
                default:
                    pinMode(ioPins[i].pin, INPUT_PULLUP); // Default fallback
                    Serial.printf("Pin %d (%s) configured as INPUT_PULLUP (default)\n", ioPins[i].pin, ioPins[i].name);
                    break;
            }
        } else if (ioPins[i].mode == 2) { // OUTPUT
            pinMode(ioPins[i].pin, OUTPUT);
            digitalWrite(ioPins[i].pin, ioPins[i].defaultState);
            Serial.printf("Pin %d (%s) configured as OUTPUT\n", ioPins[i].pin, ioPins[i].name);
        }
    }
    Serial.println("I/O pin modes applied.");
}


// ===== I/O HANDLING (FreeRTOS Task) =====
void handleIOs(void *pvParameters) {
  Serial.println("✅ I/O handling task started.");

  for (;;) { // Infinite loop for the task
    for (int i = 0; i < ioPinCount; i++) {
      if (ioPins[i].mode == 1) { // INPUT
        bool currentState = digitalRead(ioPins[i].pin);

        // Détection immédiate du changement d'état (sans debounce)
        if (currentState != ioPins[i].state) {
          ioPins[i].state = currentState;
          Serial.printf("Input '%s' (pin %d) changed to %s\n", ioPins[i].name, ioPins[i].pin, currentState ? "HIGH" : "LOW");
          
          char topic[128];
          snprintf(topic, sizeof(topic), "%s/status/%s", config.deviceName, ioPins[i].name);
          char payload[2];
          snprintf(payload, sizeof(payload), "%d", currentState ? 1 : 0);

          if (mqttEnabled && mqttClient.connected()) {
            publishMQTT(topic, payload);
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // Check inputs every 1ms (réactivité maximale)
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
