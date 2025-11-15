#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define MAX_IOS 20


// ===== CONFIGURATION PINS =====
#define RELAY_K1        16
#define RELAY_K2       17

// ===== STRUCTURES =====
// Structure for a single configurable I/O pin
struct IOPin {
  uint8_t pin;
  char name[32];
  uint8_t mode; // 0 = DISABLED, 1 = INPUT, 2 = OUTPUT
  uint8_t inputType; // For inputs: 0 = INPUT, 1 = INPUT_PULLUP, 2 = INPUT_PULLDOWN
  bool state;   // Current state (for outputs) or last read state (for inputs)
  bool defaultState; // Default state at boot for outputs
};


struct AccessLog {
  char timestamp[25];
  char ip[16];
  char resource[50];
};

// Maximum number of scheduled commands
#define MAX_SCHEDULED_COMMANDS 10

struct ScheduledCommand {
  int pin;
  int state;
  unsigned long exec_at; // Unix timestamp for execution
  bool active;
};


// Main configuration structure
struct Config {
  char deviceName[32];
  char adminPassword[32];
  
  // Network settings
  bool useStaticIP;
  char staticIP[16];
  char staticGateway[16];
  char staticSubnet[16];

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
