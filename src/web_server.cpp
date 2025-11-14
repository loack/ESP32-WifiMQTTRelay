#include "web_server.h"
#include "config.h"
#include <ElegantOTA.h>
#include <PubSubClient.h>

extern Config config;
extern IOPin ioPins[];
extern int ioPinCount;
extern PubSubClient mqttClient;
extern AccessLog accessLogs[];

extern void saveConfig();
extern void saveIOs();
extern void publishMQTT(const char* topic, const char* payload);

void setupWebServer() {
  // Page principale
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html);
  });
  
  // API - Statut système
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    JsonDocument doc;
    doc["mqtt"] = mqttClient.connected();
    doc["wifi"] = WiFi.status() == WL_CONNECTED;
    doc["ip"] = WiFi.localIP().toString();
    
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
      
      // Find IO by name
      bool found = false;
      for (int i = 0; i < ioPinCount; i++) {
        if (String(ioPins[i].name) == ioName) {
          if (ioPins[i].mode == 2) { // OUTPUT
            digitalWrite(ioPins[i].pin, state);
            ioPins[i].state = state;
            found = true;
            
            // Publish to MQTT
            char topic[128];
            snprintf(topic, sizeof(topic), "status/%s", ioPins[i].name);
            publishMQTT(topic, state ? "1" : "0");
            
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
  
  // ElegantOTA pour les mises à jour
  ElegantOTA.begin(&server);
  
  Serial.println("Web server routes configured");
}
