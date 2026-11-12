#ifndef FW_VERSION_H
#define FW_VERSION_H
#include <ArduinoJson.h>
// Identite du firmware : variante, commit git, date de compilation, indicateurs.
void fwInfoFill(JsonObject o);          // pour /api/info
int fwInfoMqtt(char* buf, size_t n);    // charge utile compacte pour <device>/info (retenue)
#endif
