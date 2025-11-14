#!/usr/bin/env python3
"""
Script de test MQTT pour ESP32 IO Controller
Permet de contrôler les relais et monitorer les états
"""

import paho.mqtt.client as mqtt
import time
import sys

# ========== CONFIGURATION ==========
MQTT_BROKER = "192.168.1.129"  # Adresse IP de votre broker MQTT
MQTT_PORT = 1883
MQTT_USER = ""  # Laisser vide si pas d'authentification
MQTT_PASSWORD = ""
MQTT_BASE_TOPIC = "esp32/io"  # Topic de base défini dans l'ESP32

# Noms des IOs/relais à contrôler
RELAY_NAMES = ["relay1", "relay2"]  # Adapter selon votre configuration

# ========== CALLBACKS MQTT ==========
def on_connect(client, userdata, flags, reason_code, properties):
    """Appelé lors de la connexion au broker"""
    if reason_code == 0:
        print(f"✓ Connecté au broker MQTT {MQTT_BROKER}:{MQTT_PORT}")
        # S'abonner à tous les topics de status
        status_topic = f"{MQTT_BASE_TOPIC}/status/#"
        client.subscribe(status_topic)
        print(f"✓ Abonné à: {status_topic}")
    else:
        print(f"✗ Échec de connexion, code: {reason_code}")
        sys.exit(1)

def on_message(client, userdata, msg):
    """Appelé lors de la réception d'un message"""
    print(f"📨 [{msg.topic}] = {msg.payload.decode()}")

def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
    """Appelé lors de la déconnexion"""
    if reason_code != 0:
        print(f"⚠ Déconnexion inattendue, code: {reason_code}")

# ========== FONCTIONS DE CONTRÔLE ==========
def set_relay(client, relay_name, state):
    """
    Active ou désactive un relais
    
    Args:
        client: Client MQTT
        relay_name: Nom du relais (ex: "relay1")
        state: True pour activer (HIGH), False pour désactiver (LOW)
    """
    topic = f"{MQTT_BASE_TOPIC}/control/{relay_name}/set"
    payload = "1" if state else "0"
    
    result = client.publish(topic, payload)
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        action = "ON" if state else "OFF"
        print(f"✓ Commande envoyée: {relay_name} -> {action}")
    else:
        print(f"✗ Erreur lors de l'envoi de la commande")

def turn_on(client, relay_name):
    """Active un relais"""
    set_relay(client, relay_name, True)

def turn_off(client, relay_name):
    """Désactive un relais"""
    set_relay(client, relay_name, False)

def toggle_relay(client, relay_name, delay=2):
    """
    Fait basculer un relais ON puis OFF avec un délai
    
    Args:
        client: Client MQTT
        relay_name: Nom du relais
        delay: Temps en secondes entre ON et OFF
    """
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

# ========== PROGRAMME PRINCIPAL ==========
def main():
    """Fonction principale"""
    print("="*50)
    print("ESP32 IO Controller - Test MQTT")
    print("="*50)
    
    # Créer le client MQTT (v2 API)
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="ESP32_Test_Client")
    
    # Configurer les callbacks
    client.on_connect = on_connect
    client.on_message = on_message
    client.on_disconnect = on_disconnect
    
    # Authentification si nécessaire
    if MQTT_USER and MQTT_PASSWORD:
        client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    
    try:
        # Connexion au broker
        print(f"Connexion à {MQTT_BROKER}:{MQTT_PORT}...")
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        
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
                
                else:
                    print("❌ Option invalide")
                
                time.sleep(0.5)
                
            except ValueError:
                print("❌ Veuillez entrer un nombre")
            except KeyboardInterrupt:
                print("\n\n👋 Interruption utilisateur")
                break
    
    except Exception as e:
        print(f"❌ Erreur: {e}")
    
    finally:
        # Nettoyer et fermer la connexion
        print("\nFermeture de la connexion...")
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    print("\n💡 N'oubliez pas de configurer:")
    print("   - MQTT_BROKER avec l'IP de votre broker")
    print("   - RELAY_NAMES avec les noms de vos IOs/relais")
    print("   - MQTT_USER et MQTT_PASSWORD si nécessaire\n")
    
    main()
