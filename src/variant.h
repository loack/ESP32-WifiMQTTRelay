#ifndef VARIANT_H
#define VARIANT_H
// Variante de firmware, choisie par l'environnement PlatformIO (pio run -e power-wifi).
// Ce depot ne contient qu'une variante : WiFi + MQTT, relais/E/S configurables, sans UDP.
#ifndef FW_VARIANT
#error "FW_VARIANT absent : compiler avec un environnement nomme (pio run -e power-wifi)"
#endif
#if !defined(FW_NET_WIFI) || !defined(FW_UDP) || !defined(FW_MQTT)
#error "indicateurs FW_NET_WIFI, FW_UDP, FW_MQTT requis (voir platformio.ini)"
#endif
#if FW_NET_WIFI != 1
#error "ce depot ne gere que le WiFi (FW_NET_WIFI=1)"
#endif
#if FW_UDP != 0
#error "pas de code UDP dans ce depot : FW_UDP doit valoir 0"
#endif
#if FW_MQTT != 1
#error "la variante power-wifi est en MQTT seul : FW_MQTT doit valoir 1"
#endif
#define FW_NET_NAME "wifi"
#define FW_IO_NAME  "relais"
#define FW_FEAT_MOTOR 0
#define FW_FEAT_MCP   0
#endif
