#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define MAX_IOS 20

// Structure for a single configurable I/O pin
struct IOPin {
  uint8_t pin;
  char name[32];
  uint8_t mode; // 0 = DISABLED, 1 = INPUT, 2 = OUTPUT
  bool state;   // Current state (for outputs) or last read state (for inputs)
  bool defaultState; // Default state at boot for outputs
};

// Main configuration structure
struct Config {
  char adminPassword[32];
  
  // MQTT Settings
  char mqttServer[64];
  int mqttPort;
  char mqttUser[32];
  char mqttPassword[32];
  char mqttTopic[32];

  // NTP Settings
  char ntpServer[64];
  long gmtOffset_sec;
  int daylightOffset_sec;

  bool initialized;
};

#endif // CONFIG_H
