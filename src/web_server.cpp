#include "web_server.h"
#include "config.h"
#include "mqtt.h"
#include <ElegantOTA.h>
#include <NTPClient.h>

extern Config config;
extern IOPin ioPins[];
extern int ioPinCount;
extern AccessLog accessLogs[];
extern NTPClient timeClient;

extern void saveConfig();
extern void saveIOs();
extern void setupNTP();

void setupWebServer() {
  // Page principale
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("API: GET /");
    request->send(200, "text/html", index_html);
  });
  
  // API - Statut système
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("API: GET /api/status");
    JsonDocument doc;
    // If mqtt subsystem is disabled, report false; otherwise report actual connected state
    doc["mqtt"] = (mqttEnabled ? mqttClient.connected() : false);
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["ip"] = WiFi.localIP().toString();
    
    // Get formatted time
    time_t now;
    struct tm * timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
    doc["time"] = timeStr;
    
    // Add IOs status
    JsonArray ios = doc["ios"].to<JsonArray>();
    for (int i = 0; i < ioPinCount; i++) {
      JsonObject io = ios.add<JsonObject>();
      io["name"] = ioPins[i].name;
      io["pin"] = ioPins[i].pin;
      io["mode"] = ioPins[i].mode;
      io["state"] = ioPins[i].state;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Contrôle IO (set output state)
  server.on("/api/io/set", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      String ioName = doc["name"].as<String>();
      bool state = doc["state"].as<bool>();
      
      Serial.printf("API: POST /api/io/set - name: %s, state: %d\n", ioName.c_str(), state);

      // Find IO by name
      bool found = false;
      for (int i = 0; i < ioPinCount; i++) {
        if (String(ioPins[i].name) == ioName) {
          if (ioPins[i].mode == 2) { // OUTPUT
            digitalWrite(ioPins[i].pin, state);
            ioPins[i].state = state;
            found = true;
            
            // Publish to MQTT only if subsystem enabled
            if (mqttEnabled) {
              char topic[128];
              snprintf(topic, sizeof(topic), "status/%s", ioPins[i].name);
              publishMQTT(topic, state ? "1" : "0");
            }
            
            request->send(200, "application/json", "{\"message\":\"IO mis à jour\"}");
          } else {
            request->send(400, "application/json", "{\"error\":\"Cet IO n'est pas une sortie\"}");
            return;
          }
          break;
        }
      }
      
      if (!found) {
        request->send(404, "application/json", "{\"error\":\"IO non trouvé\"}");
      }
    }
  );
  
  // API - Get IOs configuration
  server.on("/api/ios", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("API: GET /api/ios");
    JsonDocument doc;
    JsonArray ios = doc["ios"].to<JsonArray>();
    
    for (int i = 0; i < ioPinCount; i++) {
      JsonObject io = ios.add<JsonObject>();
      io["name"] = ioPins[i].name;
      io["pin"] = ioPins[i].pin;
      io["mode"] = ioPins[i].mode;
      io["state"] = ioPins[i].state;
      io["defaultState"] = ioPins[i].defaultState;
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Save IOs configuration
  server.on("/api/ios", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      Serial.println("API: POST /api/ios");
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      JsonArray ios = doc["ios"].as<JsonArray>();
      ioPinCount = 0;
      
      for (JsonObject io : ios) {
        if (ioPinCount < MAX_IOS) {
          ioPins[ioPinCount].pin = io["pin"];
          strlcpy(ioPins[ioPinCount].name, io["name"], 32);
          ioPins[ioPinCount].mode = io["mode"];
          ioPins[ioPinCount].defaultState = io["defaultState"] | false;
          ioPinCount++;
        }
      }
      
      saveIOs();
      
      request->send(200, "application/json", "{\"message\":\"Configuration IOs enregistrée\"}");
    }
  );
  
  
  // API - Récupérer les logs
  server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("API: GET /api/logs");
    JsonDocument doc;
    JsonArray logs = doc["logs"].to<JsonArray>();
    
    for (int i = 0; i < 100; i++) {
      if (accessLogs[i].timestamp > 0) {
        JsonObject log = logs.add<JsonObject>();
        log["timestamp"] = accessLogs[i].timestamp;
        log["code"] = accessLogs[i].code;
        log["granted"] = accessLogs[i].granted;
        log["type"] = accessLogs[i].type;
      }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Récupérer la configuration
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("API: GET /api/config");
    JsonDocument doc;
    doc["mqttServer"] = config.mqttServer;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUser"] = config.mqttUser;
    doc["mqttTopic"] = config.mqttTopic;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // API - Enregistrer la configuration
  server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      Serial.println("API: POST /api/config");
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
    
      config.mqttPort = doc["mqttPort"] | 1883;
      
      if (doc["mqttServer"].is<const char*>()) 
        strlcpy(config.mqttServer, doc["mqttServer"], 64);
      if (doc["mqttUser"].is<const char*>()) 
        strlcpy(config.mqttUser, doc["mqttUser"], 32);
      if (doc["mqttPassword"].is<const char*>()) 
        strlcpy(config.mqttPassword, doc["mqttPassword"], 32);
      if (doc["mqttTopic"].is<const char*>()) 
        strlcpy(config.mqttTopic, doc["mqttTopic"], 64);
      if (doc["adminPassword"].is<const char*>()) 
        strlcpy(config.adminPassword, doc["adminPassword"], 32);
      
      saveConfig();
      
      request->send(200, "application/json", "{\"message\":\"Configuration enregistrée\"}");
    }
  );

  // API - Get NTP config
  server.on("/api/ntp", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("API: GET /api/ntp");
    JsonDocument doc;

    time_t now;
    struct tm * timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%A, %B %d %Y %H:%M:%S", timeinfo);
    doc["time"] = timeStr;

    doc["gmtOffset"] = config.gmtOffset_sec;
    doc["daylightOffset"] = config.daylightOffset_sec;
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // API - Save NTP config
  server.on("/api/ntp", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      Serial.println("API: POST /api/ntp");
      JsonDocument doc;
      deserializeJson(doc, (const char*)data);
      
      config.gmtOffset_sec = doc["gmtOffset"] | 3600;
      config.daylightOffset_sec = doc["daylightOffset"] | 3600;
      
      saveConfig();
      
      request->send(200, "application/json", "{\"message\":\"Configuration NTP enregistrée\"}");
    }
  );

  // API - Force NTP sync
  server.on("/api/ntp/sync", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("API: POST /api/ntp/sync - DEPRECATED");
    request->send(200, "application/json", "{\"message\":\"La synchronisation se fait via MQTT maintenant.\"}");
  });
  
  // API - Activer MQTT (ne lance la connexion que lorsque activé)
  server.on("/api/mqtt/enable", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("API: POST /api/mqtt/enable");
    if (!mqttEnabled) {
      mqttEnabled = true;
      Serial.println("MQTT enabled. Initializing connection...");
      setupMQTT();
    } else {
      Serial.println("MQTT is already enabled.");
    }
    request->send(200, "application/json", "{\"message\":\"MQTT enabled\"}");
  });

  // API - Désactiver MQTT (déconnecte et stoppe les tentatives)
  server.on("/api/mqtt/disable", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println("API: POST /api/mqtt/disable");
    if (mqttEnabled) {
      mqttEnabled = false;
      if (mqttClient.connected()) {
        Serial.println("Disconnecting MQTT client...");
        mqttClient.disconnect();
      }
      Serial.println("MQTT disabled.");
    } else {
      Serial.println("MQTT is already disabled.");
    }
    request->send(200, "application/json", "{\"message\":\"MQTT disabled\"}");
  });
  
  // ElegantOTA pour les mises à jour
  ElegantOTA.begin(&server);
  
  Serial.println("Web server routes configured");
}
