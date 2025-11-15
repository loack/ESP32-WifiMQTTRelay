#include <Arduino.h>
#include <PubSubClient.h>
#include "mqtt.h"
#include <ArduinoJson.h>
#include <time.h>

#include <WiFi.h>

// define the network client and the MQTT client here (single place)
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
// MQTT active flag (default disabled so web server can be debugged first)
bool mqttEnabled = false;

// MQTT callback and helpers moved out of main.cpp

// Helper to get current time as string
String getFormattedTime() {
  char timeStr[20];
  time_t now = time(nullptr);
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
  return String(timeStr);
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String baseTopic = String(config.mqttTopic);

    // Convert payload to string for logging and parsing
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.printf("[%s] MQTT message arrived on topic [%s]: %s\n", getFormattedTime().c_str(), topic, message);

    // Handle time synchronization first, as it's a critical service
    String timeSyncTopic = baseTopic + "/time/sync";
    if (topicStr.equals(timeSyncTopic)) {
        unsigned long unix_time = atol(message);
        if (unix_time > 1000000000) { // Basic validation of timestamp
            timeval tv;
            tv.tv_sec = unix_time;
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
            Serial.printf("Time synchronized from MQTT: %lu\n", unix_time);
        }
        return; // Message handled
    }

    // Check if it's a control topic for a pin
    String controlTopicPrefix = baseTopic + "/control/";
    if (!topicStr.startsWith(controlTopicPrefix) || !topicStr.endsWith("/set")) {
        return; // Not a command for us
    }

    // Extract pin name
    String pinName = topicStr.substring(controlTopicPrefix.length(), topicStr.length() - 4);

    // Find the IO pin by name
    for (int i = 0; i < ioPinCount; i++) {
        if (String(ioPins[i].name) == pinName) {
            if (ioPins[i].mode == 2) { // OUTPUT
                bool newState = (strcmp(message, "1") == 0 || strcasecmp(message, "ON") == 0 || strcasecmp(message, "true") == 0);
                
                digitalWrite(ioPins[i].pin, newState);
                ioPins[i].state = newState;
                
                // Get execution timestamp
                time_t exec_time = time(nullptr);

                Serial.printf("Output '%s' (pin %d) set to %s\n", ioPins[i].name, ioPins[i].pin, newState ? "HIGH" : "LOW");

                // Publish the new state to the status topic as a JSON object
                JsonDocument doc;
                doc["state"] = newState ? "ON" : "OFF";
                doc["timestamp"] = exec_time;

                char jsonBuffer[128];
                serializeJson(doc, jsonBuffer);

                char statusTopic[128];
                snprintf(statusTopic, sizeof(statusTopic), "status/%s", ioPins[i].name);
                publishMQTT(statusTopic, jsonBuffer, true); // Publish as retained

            } else {
                Serial.printf("Received command for non-output pin '%s'\n", pinName.c_str());
            }
            return; // Command handled for this pin
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
    
    // Publish availability
    publishMQTT("availability", "online", true);

    // Subscribe to control topics
    String controlTopic = String(config.mqttTopic) + "/control/#";
    mqttClient.subscribe(controlTopic.c_str());
    Serial.printf("Subscribed to %s\n", controlTopic.c_str());

    // Subscribe to time sync topic
    String timeTopic = String(config.mqttTopic) + "/time/sync";
    mqttClient.subscribe(timeTopic.c_str());
    Serial.printf("Subscribed to %s\n", timeTopic.c_str());

    // Publish current state of all pins as retained messages
    for (int i = 0; i < ioPinCount; i++) {
        JsonDocument doc;
        doc["state"] = ioPins[i].state ? "ON" : "OFF";
        doc["timestamp"] = time(nullptr);

        char jsonBuffer[128];
        serializeJson(doc, jsonBuffer);

        char statusTopic[128];
        snprintf(statusTopic, sizeof(statusTopic), "status/%s", ioPins[i].name);
        publishMQTT(statusTopic, jsonBuffer, true);
    }

  } else {
    Serial.print("failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" try again in 5 seconds");
  }
}

void publishMQTT(const char* sub_topic, const char* payload, boolean retained) {
    if (mqttClient.connected()) {
        char fullTopic[128];
        snprintf(fullTopic, sizeof(fullTopic), "%s/%s", config.mqttTopic, sub_topic);
        if (mqttClient.publish(fullTopic, payload, retained)) {
            Serial.printf("[%s] MQTT message published to [%s]: %s\n", getFormattedTime().c_str(), fullTopic, payload);
        } else {
            Serial.printf("[%s] MQTT publish failed to [%s]\n", getFormattedTime().c_str(), fullTopic);
        }
    }
}
