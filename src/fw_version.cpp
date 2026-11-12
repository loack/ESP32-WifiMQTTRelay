#include <Arduino.h>
#include "fw_version.h"
#include "variant.h"
#ifndef FW_GIT
#define FW_GIT "inconnu"   // fourni par tools/fw_version.py
#endif
#ifndef FW_UDP_DEFAULT_ENABLED
#define FW_UDP_DEFAULT_ENABLED 0
#endif
#ifndef FW_UDP
#define FW_UDP FW_UDP_DEFAULT_ENABLED
#endif
void fwInfoFill(JsonObject o) {
  o["variant"] = FW_VARIANT;
  o["git"] = FW_GIT;
  o["built"] = __DATE__ " " __TIME__;
  o["net"] = FW_NET_NAME;
  o["io"] = FW_IO_NAME;
  o["motor"] = FW_FEAT_MOTOR;
  o["mcp"] = FW_FEAT_MCP;
  o["udp"] = FW_UDP;
  o["mqtt"] = FW_MQTT;
  o["freeHeap"] = ESP.getFreeHeap();
  o["sketchSize"] = ESP.getSketchSize();
  o["freeSketch"] = ESP.getFreeSketchSpace();
}
int fwInfoMqtt(char* buf, size_t n) {
  return snprintf(buf, n,
    "{\"variant\":\"%s\",\"git\":\"%s\",\"built\":\"%s\",\"net\":\"%s\",\"io\":\"%s\",\"motor\":%d,\"mcp\":%d,\"udp\":%d,\"mqtt\":%d}",
    FW_VARIANT, FW_GIT, __DATE__ " " __TIME__, FW_NET_NAME, FW_IO_NAME, FW_FEAT_MOTOR, FW_FEAT_MCP, FW_UDP, FW_MQTT);
}
