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

### Accès réseau à l'ESP32 (ping / HTTP inaccessible depuis le PC)
Si l'ESP32 a une IP fixe sur le LAN mais que votre PC ne peut pas le pinguer ni accéder au serveur web, il est possible que le trafic vers ce réseau ne passe pas directement par votre interface Ethernet (ex. VPN actif, route par défaut différente, ou VLAN/switch isolant). Une solution de contournement rapide (temporaire) est d'ajouter une route spécifique sur votre PC vers l'IP de l'ESP32.

#### Mémo VPN/Tailscale
Si Tailscale ou un autre VPN est activé sur le PC, il peut modifier la table de routage et empêcher l'accès direct aux appareils du réseau local (LAN) comme l'ESP32. Pour tester la connexion :
- Désactive temporairement Tailscale/VPN et réessaie le ping ou l'accès HTTP.
- Si besoin, ajoute une route spécifique comme indiqué ci-dessus.
- Vérifie que le réseau Windows est bien en "Privé" et que le pare-feu autorise le trafic local.
Cela peut résoudre les problèmes d'accès à l'ESP32 depuis le PC.

Exemple (remplacez `enp1s0` par votre interface Ethernet si nécessaire) :

```bash
sudo ip route add 192.168.1.80/32 dev enp1s0
```

Explications et points importants :
- Cette commande force le PC à envoyer les paquets destinés à `192.168.1.80` via l'interface `enp1s0` (couche L2 locale) au lieu de les router via une autre interface (VPN, etc.).
- La route ajoutée est temporaire : elle disparaît au redémarrage. Pour la rendre persistante, utilisez la configuration de votre gestionnaire réseau (NetworkManager, /etc/network/interfaces, ou scripts système selon votre distribution).
- Alternative : déconnecter le VPN ou corriger la table de routage/VLAN sur le switch/routeur pour que le réseau 192.168.1.0/24 soit considéré comme local.
- Si après l'ajout de la route vous voyez encore des problèmes (pas d'ARP reply), vérifiez la configuration VLAN du switch et la possibilité d'isolation AP (guest network) côté routeur.

Si tu veux, ajoute ici la sortie de `ip route show`, `ip addr`, ou du moniteur série de l'ESP (IP Address / Gateway) et je t'aide à rendre la route persistante ou à corriger la configuration réseau.

