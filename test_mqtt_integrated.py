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
import os
import platform
import json

# ========== CONFIGURATION ========== 
MQTT_PORT = 1883
MQTT_BASE_TOPIC = "esp32/io"
RELAY_NAMES = ["RelaisK1", "RelaisK2"]

# Dictionnaire pour suivre les commandes en attente de confirmation
pending_commands = {}

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

# ========== CLIENT MQTT ==========

def on_connect(client, userdata, flags, reason_code, properties):
    """Appelé lors de la connexion au broker"""
    if reason_code == 0:
        print(f"\n✓ Client connecté au broker MQTT")
        # S'abonner aux topics de statut de tous les relais
        status_topic = f"{MQTT_BASE_TOPIC}/status/#"
        client.subscribe(status_topic)
        print(f"✓ Abonné à: {status_topic}")

        # S'abonner aux topics de disponibilité
        availability_topic = f"{MQTT_BASE_TOPIC}/availability"
        client.subscribe(availability_topic)
        print(f"✓ Abonné à: {availability_topic}\n")
    else:
        print(f"✗ Échec de connexion, code: {reason_code}")

def on_message(client, userdata, msg):
    """Appelé lors de la réception d'un message"""
    current_time = time.time()
    topic = msg.topic
    payload = msg.payload.decode()

    # Gérer les messages de statut JSON
    status_prefix = f"{MQTT_BASE_TOPIC}/status/"
    if topic.startswith(status_prefix):
        relay_name = topic[len(status_prefix):]
        try:
            data = json.loads(payload)
            state = data.get("state", "N/A")
            esp_timestamp = data.get("timestamp", 0)
            
            print(f"📨 Statut reçu pour {relay_name}: {state} (depuis ESP @{esp_timestamp})")

            # Calculer la latence si une commande était en attente
            if relay_name in pending_commands:
                send_time = pending_commands.pop(relay_name)
                latency = (current_time - send_time) * 1000
                print(f"   └── ⏱️  Latence de la commande: {latency:.2f} ms")

        except json.JSONDecodeError:
            # Gérer les anciens messages non-JSON pour la compatibilité
            print(f"📨 Message (non-JSON) reçu: {topic} = {payload}")

    # Gérer les autres messages (disponibilité, etc.)
    else:
        print(f"📨 Message reçu: {topic} = {payload}")


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
    
    # Enregistrer le temps d'envoi pour calculer la latence
    pending_commands[relay_name] = time.time()
    
    result = client.publish(topic, payload, qos=1)
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        action = "ON" if state else "OFF"
        print(f"✓ Commande envoyée: {relay_name} -> {action}")
    else:
        print(f"✗ Erreur lors de l'envoi de la commande")
        # Si l'envoi échoue, retirer la commande des commandes en attente
        pending_commands.pop(relay_name, None)

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

def publish_time(client):
    """Publie le timestamp actuel sur le topic de synchronisation"""
    while True:
        if client.is_connected():
            timestamp = int(time.time())
            topic = f"{MQTT_BASE_TOPIC}/time/sync"
            client.publish(topic, str(timestamp), qos=0)
            # print(f"-> Time published: {timestamp}") # Décommenter pour debug
        time.sleep(60) # Publie toutes les 60 secondes

def restart_mosquitto():
    """Redémarre le service Mosquitto pour s'assurer qu'il est bien lancé."""
    if platform.system() == "Windows":
        print("\n🔄 Tentative de redémarrage du service Mosquitto sur Windows...")
        try:
            # Arrêter le service
            result_stop = os.system("net stop mosquitto > nul 2>&1")
            if result_stop == 0:
                print("   - Service Mosquitto arrêté.")
            
            time.sleep(2) # Attendre un peu

            # Démarrer le service
            result_start = os.system("net start mosquitto > nul 2>&1")
            if result_start == 0:
                print("   - Service Mosquitto démarré.")
                print("✓ Le service Mosquitto semble avoir redémarré avec succès.")
                time.sleep(3) # Laisse le temps au broker de s'initialiser
                return True
            else:
                print("✗ Impossible de démarrer le service Mosquitto.")
                print("  -> Assurez-vous que le script est exécuté avec les droits d'administrateur.")
                return False

        except Exception as e:
            print(f"✗ Une erreur est survenue lors de la tentative de redémarrage: {e}")
            return False
    else:
        # Pour info, si le script est utilisé sur un autre OS
        print("\nℹ️  Le redémarrage automatique de Mosquitto n'est implémenté que pour Windows.")
        return True

def check_and_restart_mosquitto(host, port):
    """Vérifie si le broker est accessible, sinon tente de le redémarrer."""
    if platform.system() != "Windows":
        print("\nℹ️  La vérification/redémarrage de Mosquitto n'est implémenté que pour Windows.")
        return

    print(f"\n🔍 Vérification du broker à l'adresse {host}:{port}...")
    try:
        with socket.create_connection((host, port), timeout=2):
            print("✓ Le broker MQTT est accessible.")
            return
    except (ConnectionRefusedError, socket.timeout, OSError):
        print("✗ Le broker MQTT ne répond pas. Tentative de redémarrage...")
        if not restart_mosquitto():
            input("\nAppuyez sur Entrée pour continuer malgré l'échec du redémarrage...")
        else:
            # Petite pause pour laisser le temps au broker de s'initialiser complètement
            time.sleep(3)

# ========== PROGRAMME PRINCIPAL ========== 
def main():
    """Fonction principale"""
    print("="*60)
    print("ESP32 IO Controller - Script de Test MQTT")
    print("="*60)
    
    local_ip = get_local_ip()
    
    # Vérifier si le broker est en ligne, sinon le redémarrer
    check_and_restart_mosquitto(local_ip, MQTT_PORT)

    print(f"\n✅ L'adresse IP de ce PC est: {local_ip}")
    
    print("\n" + "="*60)
    print("📋 CONFIGURATION REQUISE POUR L'ESP32")
    print("="*60)
    print("Assurez-vous que votre ESP32 est configuré avec les paramètres suivants:")
    print(f"  - MQTT Server: \"{local_ip}\"")
    print(f"  - MQTT Port:   {MQTT_PORT}")
    print(f"  - Base Topic:  \"{MQTT_BASE_TOPIC}\"")
    print(f"\n(Votre ESP32 doit être sur le même réseau Wi-Fi que ce PC)")
    print("="*60)
    
    broker_address = local_ip
   
    
    try:
        print(f"\n🔗 Tentative de connexion au broker: {broker_address}:{MQTT_PORT}...")
        
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
        
        # Démarrer le thread pour la publication de l'heure
        time_thread = threading.Thread(target=publish_time, args=(client,), daemon=True)
        time_thread.start()

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
                
                time.sleep(0.3)
                
            except ValueError:
                print("❌ Veuillez entrer un nombre")
            except KeyboardInterrupt:
                print("\n\n👋 Interruption utilisateur")
                break
    
    except ConnectionRefusedError:
        print(f"\n❌ IMPOSSIBLE DE SE CONNECTER AU BROKER {broker_address}:{MQTT_PORT}")
        print("\n💡 Solutions:")
        print("   1. Assurez-vous que Mosquitto est bien démarré sur ce PC.")
        print("   2. Vérifiez que votre pare-feu ne bloque pas le port 1883.")
        print("   3. Essayez de redémarrer Mosquitto.")
    except Exception as e:
        print(f"❌ Une erreur inattendue est survenue: {e}")
    
    finally:
        # Nettoyer et fermer la connexion
        print("\nFermeture de la connexion...")
        try:
            client.loop_stop()
            client.disconnect()
        except:
            pass

if __name__ == "__main__":
    main()
