#!/usr/bin/env python3
"""
Script de test MQTT avec broker intégré pour ESP32 IO Controller
Combine un broker MQTT simple et un client de test
"""

import paho.mqtt.client as mqtt
import threading
import time
import sys
import socket

# ========== CONFIGURATION ==========
MQTT_PORT = 1883
MQTT_BASE_TOPIC = "esp32/io"
RELAY_NAMES = ["relay1", "relay2"]

def get_local_ip():
    """Récupère l'adresse IP locale"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"

# ========== BROKER MQTT SIMPLE ==========
class SimpleMQTTBroker:
    """Broker MQTT simple basé sur un dictionnaire de topics"""
    
    def __init__(self, port=1883):
        self.port = port
        self.clients = []
        self.subscriptions = {}  # client -> [topics]
        self.messages = {}  # topic -> message
        self.running = False
        self.local_ip = get_local_ip()
        
    def start(self):
        """Démarre le broker (simulation)"""
        self.running = True
        print(f"🟢 Broker MQTT simulé démarré")
        print(f"   IP: {self.local_ip}")
        print(f"   Port: {self.port}")
        print(f"   Topics base: {MQTT_BASE_TOPIC}/#")
        
    def stop(self):
        """Arrête le broker"""
        self.running = False
        print("🔴 Broker MQTT arrêté")

# ========== CLIENT MQTT ==========
broker_sim = SimpleMQTTBroker(MQTT_PORT)

def on_connect(client, userdata, flags, reason_code, properties):
    """Appelé lors de la connexion au broker"""
    if reason_code == 0:
        print(f"\n✓ Client connecté au broker MQTT")
        status_topic = f"{MQTT_BASE_TOPIC}/status/#"
        client.subscribe(status_topic)
        print(f"✓ Abonné à: {status_topic}\n")
    else:
        print(f"✗ Échec de connexion, code: {reason_code}")

def on_message(client, userdata, msg):
    """Appelé lors de la réception d'un message"""
    timestamp = time.strftime("%H:%M:%S")
    print(f"📨 [{timestamp}] {msg.topic} = {msg.payload.decode()}")

def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    """Appelé lors de la déconnexion"""
    if reason_code != 0:
        print(f"⚠ Déconnexion inattendue, code: {reason_code}")

def on_publish(client, userdata, mid, reason_code=None, properties=None):
    """Appelé quand un message est publié"""
    pass  # Silencieux pour ne pas polluer la console

# ========== FONCTIONS DE CONTRÔLE ==========
def set_relay(client, relay_name, state):
    """Active ou désactive un relais"""
    topic = f"{MQTT_BASE_TOPIC}/control/{relay_name}/set"
    payload = "1" if state else "0"
    
    result = client.publish(topic, payload, qos=1)
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        action = "ON" if state else "OFF"
        print(f"✓ Commande: {relay_name} -> {action}")
    else:
        print(f"✗ Erreur lors de l'envoi")

def turn_on(client, relay_name):
    """Active un relais"""
    set_relay(client, relay_name, True)

def turn_off(client, relay_name):
    """Désactive un relais"""
    set_relay(client, relay_name, False)

def toggle_relay(client, relay_name, delay=2):
    """Fait basculer un relais ON puis OFF avec un délai"""
    print(f"\n🔄 Test toggle {relay_name}...")
    turn_on(client, relay_name)
    time.sleep(delay)
    turn_off(client, relay_name)

# ========== MENU INTERACTIF ==========
def show_menu():
    """Affiche le menu des commandes"""
    print("\n" + "="*50)
    print("COMMANDES DISPONIBLES:")
    print("="*50)
    for i, relay in enumerate(RELAY_NAMES, 1):
        print(f"{i}. Activer {relay}")
        print(f"{i+len(RELAY_NAMES)}. Désactiver {relay}")
    
    offset = len(RELAY_NAMES) * 2
    print(f"{offset+1}. Toggle tous les relais")
    print(f"{offset+2}. Test séquentiel")
    print(f"{offset+3}. Afficher l'IP du broker")
    print("0. Quitter")
    print("="*50)

def test_sequence(client):
    """Test séquentiel de tous les relais"""
    print("\n🧪 Début du test séquentiel...")
    for relay in RELAY_NAMES:
        print(f"\n→ Test de {relay}")
        toggle_relay(client, relay, delay=1.5)
        time.sleep(0.5)
    print("\n✓ Test séquentiel terminé")

def toggle_all(client):
    """Active puis désactive tous les relais"""
    print("\n🔄 Activation de tous les relais...")
    for relay in RELAY_NAMES:
        turn_on(client, relay)
        time.sleep(0.2)
    
    time.sleep(2)
    
    print("\n🔄 Désactivation de tous les relais...")
    for relay in RELAY_NAMES:
        turn_off(client, relay)
        time.sleep(0.2)

def show_broker_info():
    """Affiche les informations du broker"""
    print("\n" + "="*50)
    print("INFORMATION BROKER MQTT")
    print("="*50)
    print(f"IP locale: {broker_sim.local_ip}")
    print(f"Port: {broker_sim.port}")
    print(f"\n📋 Configuration ESP32:")
    print(f"   MQTT Server: {broker_sim.local_ip}")
    print(f"   MQTT Port: {broker_sim.port}")
    print(f"   MQTT Topic: {MQTT_BASE_TOPIC}")
    print("="*50)

# ========== PROGRAMME PRINCIPAL ==========
def main():
    """Fonction principale"""
    print("="*60)
    print("ESP32 IO Controller - Test MQTT avec Broker Externe")
    print("="*60)
    
    local_ip = get_local_ip()
    
    print(f"\n⚠️  IMPORTANT: Ce script nécessite un broker MQTT externe!")
    print(f"\n📋 Options:")
    print(f"   1. Installer Mosquitto localement")
    print(f"   2. Utiliser un broker cloud: test.mosquitto.org")
    print(f"   3. Utiliser un broker sur votre réseau")
    
    print(f"\n💡 Configurez votre ESP32 avec:")
    print(f"   MQTT Server: {local_ip} (si Mosquitto local)")
    print(f"   MQTT Port: 1883")
    print(f"   MQTT Topic: {MQTT_BASE_TOPIC}")
    
    print("\n" + "="*60)
    
    # Demander l'adresse du broker
    print("\nEntrez l'adresse du broker MQTT à utiliser:")
    print(f"  - Appuyez sur Entrée pour utiliser: {local_ip} (local)")
    print(f"  - Ou entrez une autre adresse (ex: test.mosquitto.org)")
    
    try:
        broker_address = "test.mosquitto.org"
        print(f"\nUtilisation du broker de test : {broker_address}")
        
        print(f"\n🔗 Connexion au broker: {broker_address}:{MQTT_PORT}")
        
        # Créer le client MQTT
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="ESP32_Test_Client")
        
        # Configurer les callbacks
        client.on_connect = on_connect
        client.on_message = on_message
        client.on_disconnect = on_disconnect
        client.on_publish = on_publish
        
        # Connexion au broker
        client.connect(broker_address, MQTT_PORT, 60)
        
        # Démarrer la boucle réseau en arrière-plan
        client.loop_start()
        
        # Attendre que la connexion soit établie
        time.sleep(1)
        
        # Menu interactif
        while True:
            show_menu()
            try:
                choice = input("\nChoisissez une option: ").strip()
                
                if choice == "0":
                    print("\n👋 Au revoir!")
                    break
                
                choice = int(choice)
                num_relays = len(RELAY_NAMES)
                
                # Activer un relais
                if 1 <= choice <= num_relays:
                    turn_on(client, RELAY_NAMES[choice-1])
                
                # Désactiver un relais
                elif num_relays+1 <= choice <= num_relays*2:
                    turn_off(client, RELAY_NAMES[choice-num_relays-1])
                
                # Toggle tous
                elif choice == num_relays*2 + 1:
                    toggle_all(client)
                
                # Test séquentiel
                elif choice == num_relays*2 + 2:
                    test_sequence(client)
                
                # Afficher info broker
                elif choice == num_relays*2 + 3:
                    print(f"\n📋 Broker utilisé: {broker_address}:{MQTT_PORT}")
                
                else:
                    print("❌ Option invalide")
                
                time.sleep(0.3)
                
            except ValueError:
                print("❌ Veuillez entrer un nombre")
            except KeyboardInterrupt:
                print("\n\n👋 Interruption utilisateur")
                break
    
    except ConnectionRefusedError:
        print(f"\n❌ Impossible de se connecter au broker {broker_address}:{MQTT_PORT}")
        print("\n💡 Solutions:")
        print("   1. Installer et démarrer Mosquitto:")
        print("      Windows: choco install mosquitto")
        print("      Linux: sudo apt-get install mosquitto")
        print("   2. Utiliser: python mqtt_broker.py")
        print("   3. Utiliser un broker cloud: test.mosquitto.org")
    except Exception as e:
        print(f"❌ Erreur: {e}")
    
    finally:
        # Nettoyer et fermer la connexion
        print("\nFermeture de la connexion...")
        try:
            client.loop_stop()
            client.disconnect()
        except:
            pass

if __name__ == "__main__":
    print("\n💡 Assurez-vous qu'un broker MQTT est démarré!")
    print("   Exécutez 'mosquitto' dans un autre terminal")
    print("   Ou utilisez: python mqtt_broker.py\n")
    
    main()
