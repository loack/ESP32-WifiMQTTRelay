# Guide de test MQTT pour ESP32

## Installation du broker MQTT

### Option 1: Mosquitto (Recommandé)

**Windows:**
```powershell
# Avec Chocolatey
choco install mosquitto

# OU télécharger depuis https://mosquitto.org/download/
```

**Linux:**
```bash
sudo apt-get update
sudo apt-get install mosquitto mosquitto-clients
```

**macOS:**
```bash
brew install mosquitto
```

### Option 2: Broker cloud gratuit
- `test.mosquitto.org` (port 1883)
- `broker.hivemq.com` (port 1883)

## Démarrage du broker

### Mosquitto local
```powershell
# Démarrer mosquitto
mosquitto -v

# OU utiliser le script Python
python mqtt_broker.py
```

## Configuration ESP32

1. Trouvez votre IP locale: `ipconfig` (Windows) ou `ifconfig` (Linux/Mac)
2. Configurez dans l'ESP32:
   - MQTT Server: `192.168.1.XXX` (votre IP)
   - MQTT Port: `1883`
   - MQTT Topic: `esp32/io`

## Utilisation du script de test

### Script de test simple
```bash
# Installer la dépendance
pip install paho-mqtt

# Lancer le test
python test_mqtt_relay.py
```

### Script de test intégré
```bash
# Lancer le test (vous demandera l'adresse du broker)
python test_mqtt_integrated.py
```

## Structure des topics MQTT

### Commandes (ESP32 subscribe)
```
esp32/io/control/<nom_du_relais>/set
```
Payload: `"0"` (OFF) ou `"1"` (ON)

Exemple:
```
esp32/io/control/relay1/set → "1"  # Active relay1
esp32/io/control/relay2/set → "0"  # Désactive relay2
```

### Status (ESP32 publish)
```
esp32/io/status/<nom_du_relais>
```
Payload: `"0"` (OFF) ou `"1"` (ON)

## Test manuel avec mosquitto_pub/sub

### Écouter tous les messages
```bash
mosquitto_sub -h localhost -t "esp32/io/#" -v
```

### Envoyer une commande
```bash
# Activer relay1
mosquitto_pub -h localhost -t "esp32/io/control/relay1/set" -m "1"

# Désactiver relay1
mosquitto_pub -h localhost -t "esp32/io/control/relay1/set" -m "0"
```

## Dépannage

### Erreur "Connection refused"
- Vérifiez que mosquitto est démarré: `mosquitto -v`
- Vérifiez le pare-feu (port 1883)

### ESP32 "DNS Failed"
- Le champ MQTT Server est vide dans l'ESP32
- Configurez l'IP du broker via l'interface web ou le code

### Aucun message reçu
- Vérifiez que l'ESP32 est connecté au broker (Serial Monitor)
- Vérifiez que les topics correspondent
- Vérifiez que les relais sont configurés dans l'ESP32
