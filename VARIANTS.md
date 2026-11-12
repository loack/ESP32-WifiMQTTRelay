# Variantes de firmware (ESP32-WifiMQTTRelay)

Ce depot ne porte qu'une variante, nommee par l'environnement PlatformIO :

| Environnement | Reseau | E/S | UDP | MQTT | Cartes |
|---|---|---|---|---|---|
| `power-wifi` | WiFi | relais K1/K2 et E/S configurables | non (aucun code UDP) | oui | power-minus, power-cortex, ESP32 generique |

`power-wifi` est le firmware d'origine, inchange : `src/main.cpp`, `config.h` et `mqtt.h` ne different pas de `master`
(sequence de demarrage, broches, etats par defaut identiques). Les etats au demarrage des sorties (GPIO 16/17 pour K1/K2)
viennent de la configuration d'E/S enregistree en NVS, pas du code.

Ajouts (sans effet sur les broches) :
- `src/variant.h` : indicateurs `FW_VARIANT`, `FW_NET_WIFI`, `FW_UDP`, `FW_MQTT` et controles de coherence a la compilation (`FW_UDP=1` est refuse : pas de code UDP ici).
- `GET /api/info` : variante, commit git, date de compilation, indicateurs, memoire et place libre pour l'OTA.
- MQTT : message retenu `<device>/info` publie a chaque connexion au broker (meme contenu, sans la memoire).
- `tools/fw_version.py` : injecte le commit (`-modifie` si l'arbre suivi est modifie).

Compilation : `pio run -e power-wifi`. Mise a jour : OTA par `/update` (firmware seul), jamais le systeme de fichiers ni un effacement.
