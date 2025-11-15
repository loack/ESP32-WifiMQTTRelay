#include <Arduino.h>
#include <PubSubClient.h>
#include "mqtt.h"

#include <WiFi.h>

// define the network client and the MQTT client here (single place)
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
// MQTT active flag (default disabled so web server can be debugged first)
bool mqttEnabled = false;

// MQTT callback and helpers moved out of main.cpp
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

    // Handle time synchronization
    if (topicStr.endsWith("time/sync")) {
        // Convert message to long
        unsigned long unix_time = atol(message);
        if (unix_time > 1000000000) { // Basic validation
            timeval tv;
            tv.tv_sec = unix_time;
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
            Serial.printf("Time synchronized from MQTT: %s\n", message);
        }
        return; // Message handled
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
    String controlTopic = String(config.mqttTopic) + "/control/#";
    mqttClient.subscribe(controlTopic.c_str());
    Serial.printf("Subscribed to %s\n", controlTopic.c_str());

    // Subscribe to time sync topic
    String timeTopic = String(config.mqttTopic) + "/time/sync";
    mqttClient.subscribe(timeTopic.c_str());
    Serial.printf("Subscribed to %s\n", timeTopic.c_str());

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
