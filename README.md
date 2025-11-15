# Contrôleur d'Entrées/Sorties Générique pour ESP32 via MQTT et Web

Ce projet transforme un ESP32 en un contrôleur d'entrées/sorties (I/O) polyvalent, entièrement configurable et contrôlable à distance via une interface web et le protocole MQTT. Il est conçu pour être robuste, réactif et adaptable à divers besoins domotiques ou de prototypage.

L'une de ses fonctionnalités phares est la capacité d'exécuter des commandes de manière synchronisée sur plusieurs appareils grâce à des **commandes programmées par timestamp**.

## Fonctionnalités Principales

- **Interface Web Intégrée** : Pour la configuration des I/O, des paramètres système (MQTT) et le contrôle manuel.
- **Configuration Dynamique des I/O** : Chaque pin GPIO compatible peut être configuré comme :
  - **Entrée (INPUT)** : avec résistance de pull-up interne.
  - **Sortie (OUTPUT)** : avec un état par défaut au démarrage.
- **Contrôle MQTT Complet** : Toutes les I/O configurées sont contrôlables et leur état est rapporté via MQTT.
- **Exécution de Commandes Programmées (Scheduled)** : Envoyez des commandes MQTT avec un timestamp d'exécution futur pour déclencher des actions synchronisées à la milliseconde près sur plusieurs appareils.
- **Synchronisation Temporelle via MQTT** : L'horloge interne de l'ESP32 est synchronisée à partir d'un topic MQTT, garantissant une base de temps commune pour les commandes programmées.
- **Mises à Jour Over-The-Air (OTA)** : Mettez à jour le firmware directement depuis l'interface web (`/update`).
- **Portail Captif (WiFiManager)** : Configuration facile du WiFi au premier démarrage via un point d'accès dédié.
- **Haute Réactivité** : Utilisation de FreeRTOS pour dédier une tâche à la surveillance des entrées, et désactivation du mode d'économie d'énergie du WiFi pour une faible latence.
- **Persistance des Données** : La configuration (WiFi, MQTT, I/O) est sauvegardée en mémoire flash et restaurée au redémarrage.

## Architecture du Firmware

Le projet est structuré de manière modulaire pour une meilleure lisibilité et maintenance :

- `main.cpp` : Point d'entrée principal. Gère l'initialisation, la connexion WiFi, la création des tâches et la boucle principale qui traite les commandes programmées et la connexion MQTT.
- `web_server.cpp` : Met en place le serveur web asynchrone et définit toutes les routes de l'API REST pour l'interface web.
- `mqtt.cpp` : Gère l'intégralité de la logique MQTT : connexion, souscription, publication et, surtout, l'analyse des messages de commande (JSON).
- `config.h` : Définit les structures de données globales (`Config`, `IOPin`, `ScheduledCommand`) utilisées à travers le projet.
- **Tâche FreeRTOS (`handleIOs`)** : Une tâche dédiée s'exécute sur un cœur séparé pour lire l'état des entrées de manière non-bloquante, avec un système d'anti-rebond (debounce).
- **SPIFFS** : Le système de fichiers embarqué est utilisé pour stocker les fichiers de l'interface web (ex: `index.html`).
- **Preferences** : Cette bibliothèque est utilisée pour sauvegarder de manière persistante la configuration dans la mémoire flash non volatile.

## Premier Démarrage et Configuration

1.  **Flasher le Firmware** : Compilez et téléversez le projet sur votre ESP32 avec PlatformIO.
2.  **Portail Captif** : Au premier démarrage (ou après un reset WiFi), l'ESP32 crée un point d'accès WiFi nommé **`ESP32-WifiMQTTRelay-Setup`**.
3.  **Connexion** : Connectez-vous à ce réseau avec votre téléphone ou ordinateur. Un portail captif devrait s'ouvrir automatiquement. Sinon, ouvrez un navigateur et allez à l'adresse `http://192.168.4.1`.
4.  **Configurer le WiFi** : Sélectionnez votre réseau WiFi domestique, entrez le mot de passe et enregistrez. L'ESP32 va se connecter et redémarrer.
5.  **Accéder à l'Interface Web** : Ouvrez le moniteur série pour voir l'adresse IP attribuée à l'ESP32. Accédez à cette adresse IP dans votre navigateur pour commencer la configuration.

## API de Contrôle MQTT

L'API MQTT est le cœur du système pour l'automatisation. Toutes les communications sont basées sur un **topic de base** configurable depuis l'interface web (défaut : `esp32/io`).

---

### 1. Synchronisation Temporelle (Essentiel)

Pour que les commandes programmées fonctionnent, l'horloge de l'ESP32 doit être synchronisée.

- **Topic** : `<base_topic>/time/sync`
- **Payload** : Un timestamp Unix (nombre de secondes depuis le 1er Janvier 1970).
- **Exemple** : Le script `test_mqtt_integrated.py` publie automatiquement le temps sur ce topic toutes les 60 secondes.

```bash
# Exemple de publication manuelle
mosquitto_pub -h <broker_ip> -t "esp32/io/time/sync" -m "1672531200"
```

---

### 2. Contrôle des Sorties (Commandes)

Les commandes sont envoyées au format JSON, ce qui permet de spécifier une exécution immédiate ou programmée.

- **Topic** : `<base_topic>/control/<nom_du_pin>/set`
  - `<nom_du_pin>` est le nom que vous avez donné à l'I/O dans l'interface web (ex: `RelaisK1`).

#### Exécution Immédiate

Pour changer l'état d'une sortie instantanément.

- **Payload JSON** : `{"state": 1}` (pour ON) ou `{"state": 0}` (pour OFF).

- **Exemple** :
  ```json
  {
    "state": 1
  }
  ```
  ```bash
  mosquitto_pub -h <broker_ip> -t "esp32/io/control/RelaisK1/set" -m '{"state": 1}'
  ```

#### Exécution Programmée (Scheduled)

Pour exécuter une commande à un moment précis dans le futur.

- **Payload JSON** : `{"state": 1, "exec_at": <timestamp>}`
  - `exec_at` est le timestamp Unix auquel la commande doit être exécutée.

- **Exemple** (exécuter dans le futur) :
  ```json
  {
    "state": 1,
    "exec_at": 1763241600
  }
  ```
  ```bash
  mosquitto_pub -h <broker_ip> -t "esp32/io/control/RelaisK1/set" -m '{"state": 1, "exec_at": 1763241600}'
  ```
L'ESP32 recevra la commande, la mettra en file d'attente et l'exécutera précisément lorsque son horloge interne (synchronisée) atteindra le timestamp `exec_at`.

---

### 3. Lecture des États (Status)

L'ESP32 publie le changement d'état de n'importe quelle I/O (entrée ou sortie) sur un topic de statut.

- **Topic** : `<base_topic>/status/<nom_du_pin>`
- **Payload JSON** : `{"state": <0_ou_1>, "timestamp": <timestamp>}`
  - `state` : L'état actuel du pin (0 pour LOW, 1 pour HIGH).
  - `timestamp` : Le timestamp Unix précis auquel le changement d'état a eu lieu.

- **Exemple de message reçu de l'ESP32** :
  ```json
  {
    "state": 1,
    "timestamp": 1763241599
  }
  ```
Ce message indique que le pin `RelaisK1` est passé à l'état `HIGH` au timestamp `1763241599`.

---

### 4. Disponibilité de l'Appareil

L'ESP32 notifie de sa présence sur le réseau.

- **Topic** : `<base_topic>/availability`
- **Payload** :
  - `online` : Publié lorsque l'ESP32 se connecte au broker MQTT.
  - `offline` : Peut être configuré comme message LWT (Last Will and Testament) sur le broker pour une détection de déconnexion.

## Script de Test Python

Le script `test_mqtt_integrated.py` est un outil puissant pour interagir avec l'ESP32. Il fournit :
- Un client MQTT pour envoyer des commandes et recevoir des statuts.
- Un menu interactif pour contrôler les relais.
- La publication automatique du timestamp pour la synchronisation.
- Une option pour tester les commandes programmées.
- Des calculs de latence pour les commandes immédiates et de précision pour les commandes programmées.
