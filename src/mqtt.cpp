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

void executeCommand(int pin, int state) {
  digitalWrite(pin, state);
  for (int i = 0; i < ioPinCount; i++) {
    if (ioPins[i].pin == pin) {
      ioPins[i].state = state;
      break;
    }
  }

  // Publish status
  char topic[128];
  int pinIndex = -1;
  for(int i=0; i<ioPinCount; i++) {
    if(ioPins[i].pin == pin) {
      pinIndex = i;
      break;
    }
  }
  if(pinIndex != -1) {
    snprintf(topic, sizeof(topic), "%s/status/%s", config.deviceName, ioPins[pinIndex].name);
    
    JsonDocument doc;
    doc["state"] = state;
    doc["timestamp"] = time(nullptr);
    
    char payload[64];
    serializeJson(doc, payload);

    if (mqttEnabled && mqttClient.connected()) {
      publishMQTT(topic, payload);
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String topicStr = String(topic);
    String baseTopic = String(config.deviceName);

    // Convert payload to string for logging and parsing
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.printf("[%s] MQTT message arrived on topic [%s]: %s\n", getFormattedTime().c_str(), topic, message);

    // Handle time synchronization first, as it's a critical service
    // Le topic de temps est commun à tous les appareils
    if (topicStr.equals("esp32/time/sync")) {
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
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, payload, length);

                if (error) {
                    Serial.print(F("deserializeJson() failed: "));
                    Serial.println(error.c_str());
                    // Fallback for simple "0" or "1" commands
                    int state = atoi(message);
                    executeCommand(ioPins[i].pin, state);
                    return;
                }

                int state = doc["state"];
                unsigned long exec_at = doc["exec_at"];

                if (exec_at > 0) {
                    // Schedule command
                    bool scheduled = false;
                    for (int j = 0; j < MAX_SCHEDULED_COMMANDS; j++) {
                        if (!scheduledCommands[j].active) {
                            scheduledCommands[j].pin = ioPins[i].pin;
                            scheduledCommands[j].state = state;
                            scheduledCommands[j].exec_at = exec_at;
                            scheduledCommands[j].active = true;
                            scheduled = true;
                            Serial.printf("Command for pin %d scheduled at %lu\n", ioPins[i].pin, exec_at);
                            break;
                        }
                    }
                    if (!scheduled) {
                        Serial.println("Scheduled command queue is full!");
                    }
                } else {
                    // Execute immediately
                    executeCommand(ioPins[i].pin, state);
                }

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
    Serial.println();
    Serial.println("========================================");
    Serial.println("✓ Client MQTT connecté au broker");
    
    // Publish availability
    char availabilityTopic[128];
    snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/availability", config.deviceName);
    publishMQTT(availabilityTopic, "online", true);

    // Subscribe to control topics
    String controlTopic = String(config.deviceName) + "/control/#";
    mqttClient.subscribe(controlTopic.c_str());
    Serial.printf("✓ Abonné à: %s\n", controlTopic.c_str());

    // Subscribe to time sync topic (commun à tous les ESP32)
    mqttClient.subscribe("esp32/time/sync");
    Serial.printf("✓ Abonné à: esp32/time/sync\n");
    Serial.println("========================================");
    Serial.println();

    // Publish current state of all pins as retained messages
    for (int i = 0; i < ioPinCount; i++) {
        JsonDocument doc;
        doc["state"] = ioPins[i].state ? "ON" : "OFF";
        doc["timestamp"] = time(nullptr);

        char jsonBuffer[128];
        serializeJson(doc, jsonBuffer);

        char statusTopic[128];
        snprintf(statusTopic, sizeof(statusTopic), "%s/status/%s", config.deviceName, ioPins[i].name);
        publishMQTT(statusTopic, jsonBuffer, true);
    }

  } else {
    Serial.print("failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" try again in 5 seconds");
  }
}

void publishMQTT(const char* topic, const char* payload, boolean retained) {
    if (mqttClient.connected()) {
        if (mqttClient.publish(topic, payload, retained)) {
            Serial.printf("[%s] MQTT message published to [%s]: %s\n", getFormattedTime().c_str(), topic, payload);
        } else {
            Serial.printf("[%s] MQTT publish failed to [%s]\n", getFormattedTime().c_str(), topic);
        }
    }
}
