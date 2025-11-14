#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"

// External variables declared in main.cpp
extern AsyncWebServer server;
extern Config config;
extern IOPin ioPins[MAX_IOS];
extern int ioPinCount;

// External functions declared in main.cpp
extern void saveConfig();
extern void saveIOs();
extern void applyIOPinModes();
extern void publishMQTT(const char* sub_topic, const char* payload);

// Forward declarations
void handleGetConfig(AsyncWebServerRequest *request);
void handleSaveConfig(AsyncWebServerRequest *request);
void handleGetIOs(AsyncWebServerRequest *request);
void handleSaveIOs(AsyncWebServerRequest *request);
void handleGetStatus(AsyncWebServerRequest *request);
void handleNotFound(AsyncWebServerRequest *request);

void setupWebServer() {
    // Route to get current configuration
    server.on("/api/config", HTTP_GET, handleGetConfig);

    // Route to save configuration
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleSaveConfig);

    // Route to get I/O pin configuration
    server.on("/api/ios", HTTP_GET, handleGetIOs);

    // Route to save I/O pin configuration
    server.on("/api/ios", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, handleSaveIOs);

    // Route to get system status
    server.on("/api/status", HTTP_GET, handleGetStatus);

    // Serve the main web page
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // Handle not found
    server.onNotFound(handleNotFound);
}

void handleGetConfig(AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;
    doc["mqttServer"] = config.mqttServer;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUser"] = config.mqttUser;
    doc["mqttTopic"] = config.mqttTopic;
    doc["ntpServer"] = config.ntpServer;
    doc["gmtOffset"] = config.gmtOffset_sec;
    doc["daylightOffset"] = config.daylightOffset_sec;

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleSaveConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data, len);
        if (error) {
            request->send(400, "text/plain", "Invalid JSON");
            return;
        }

        strlcpy(config.mqttServer, doc["mqttServer"] | "", sizeof(config.mqttServer));
        config.mqttPort = doc["mqttPort"] | 1883;
        strlcpy(config.mqttUser, doc["mqttUser"] | "", sizeof(config.mqttUser));
        strlcpy(config.mqttPassword, doc["mqttPassword"] | "", sizeof(config.mqttPassword)); // Note: password might not be in the form
        strlcpy(config.mqttTopic, doc["mqttTopic"] | "esp32/io", sizeof(config.mqttTopic));
        strlcpy(config.ntpServer, doc["ntpServer"] | "pool.ntp.org", sizeof(config.ntpServer));
        config.gmtOffset_sec = doc["gmtOffset"] | 3600;
        config.daylightOffset_sec = doc["daylightOffset"] | 3600;

        saveConfig();
        request->send(200, "text/plain", "Configuration saved. Restarting...");
        delay(1000);
        ESP.restart();
    }
}

void handleGetIOs(AsyncWebServerRequest *request) {
    StaticJsonDocument<1024> doc;
    JsonArray ioArray = doc.to<JsonArray>();

    for (int i = 0; i < ioPinCount; i++) {
        JsonObject io = ioArray.createNestedObject();
        io["pin"] = ioPins[i].pin;
        io["name"] = ioPins[i].name;
        io["mode"] = ioPins[i].mode;
        io["state"] = ioPins[i].state;
        io["defaultState"] = ioPins[i].defaultState;
    }

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleSaveIOs(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index == 0) {
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data, len);
        if (error) {
            request->send(400, "text/plain", "Invalid JSON");
            return;
        }

        JsonArray ioArray = doc.as<JsonArray>();
        ioPinCount = 0;
        for (JsonObject io : ioArray) {
            if (ioPinCount < MAX_IOS) {
                ioPins[ioPinCount].pin = io["pin"];
                strlcpy(ioPins[ioPinCount].name, io["name"], sizeof(ioPins[ioPinCount].name));
                ioPins[ioPinCount].mode = io["mode"];
                ioPins[ioPinCount].defaultState = io["defaultState"];
                ioPinCount++;
            }
        }
        saveIOs();
        applyIOPinModes();
        request->send(200, "text/plain", "I/O configuration saved.");
    }
}

void handleGetStatus(AsyncWebServerRequest *request) {
    StaticJsonDocument<512> doc;
    doc["wifi_status"] = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
    doc["ip"] = WiFi.localIP().toString();
    doc["eth_status"] = eth_connected ? "Connected" : "Disconnected";
    if(eth_connected) doc["eth_ip"] = ETH.localIP().toString();
    doc["mqtt_status"] = mqttClient.connected() ? "Connected" : "Disconnected";
    doc["time"] = timeClient.getFormattedTime();
    doc["heap"] = ESP.getFreeHeap();

    String output;
    serializeJson(doc, output);
    request->send(200, "application/json", output);
}

void handleNotFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}
