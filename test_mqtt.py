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
DEVICE_NAME = "lilygo"  # Nom de l'appareil ESP32 (doit correspondre au nom configuré sur l'ESP32)
RELAY_NAMES = ["RelaisK1", "RelaisK2","RelaisK3","RelaisK4"]
RELAY_NAMES = ["RelaisK1", "RelaisK2"]

# Dictionnaire pour suivre les commandes en attente de confirmation
pending_commands = {}

# Mesure de latence réseau
latency_tracker = {
    'samples': [],
    'max_samples': 20,
    'last_measurement': 0,
    'avg_latency_us': 0,
    'ping_times': {}
}

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
        status_topic = f"{DEVICE_NAME}/status/#"
        client.subscribe(status_topic)
        print(f"✓ Abonné à: {status_topic}")

        # S'abonner aux topics de disponibilité
        availability_topic = f"{DEVICE_NAME}/availability"
        client.subscribe(availability_topic)
        print(f"✓ Abonné à: {availability_topic}")
        
        # S'abonner au topic de temps commun
        client.subscribe("esp32/time/sync")
        print(f"✓ Abonné à: esp32/time/sync")
        
        # S'abonner au topic pong pour mesurer la latence
        pong_topic = f"{DEVICE_NAME}/pong"
        client.subscribe(pong_topic)
        print(f"✓ Abonné à: {pong_topic}\n")
    else:
        print(f"✗ Échec de connexion, code: {reason_code}")

def on_message(client, userdata, msg):
    """Appelé lors de la réception d'un message"""
    receipt_time = time.time()
    topic = msg.topic
    payload = msg.payload.decode()
    
    # Gérer les réponses pong pour mesurer la latence
    if topic.endswith("/pong"):
        try:
            data = json.loads(payload)
            ping_payload = data.get("ping_payload")
            
            if ping_payload and ping_payload in latency_tracker['ping_times']:
                ping_time = latency_tracker['ping_times'].pop(ping_payload)
                rtt = (receipt_time - ping_time) * 1000000  # en microsecondes
                
                # Ajouter à l'échantillon
                latency_tracker['samples'].append(rtt)
                if len(latency_tracker['samples']) > latency_tracker['max_samples']:
                    latency_tracker['samples'].pop(0)
                
                # Calculer la latence moyenne (unidirectionnelle = RTT / 2)
                avg_rtt = sum(latency_tracker['samples']) / len(latency_tracker['samples'])
                latency_tracker['avg_latency_us'] = int(avg_rtt / 2)
                
                # Envoyer la latence estimée à l'ESP32
                latency_topic = f"{DEVICE_NAME}/latency"
                latency_payload = json.dumps({
                    "estimated_latency_us": latency_tracker['avg_latency_us']
                })
                client.publish(latency_topic, latency_payload)
                
                print(f"📡 RTT: {rtt/1000:.2f}ms | Latence estimée: {latency_tracker['avg_latency_us']/1000:.2f}ms")
        except (json.JSONDecodeError, KeyError):
            pass
        return

    # Gérer les messages de statut JSON
    status_prefix = f"{DEVICE_NAME}/status/"
    if topic.startswith(status_prefix):
        relay_name = topic[len(status_prefix):]
        try:
            # Essayer de parser comme JSON d'abord
            data = json.loads(payload)
            
            # Si c'est un objet JSON avec state et timestamp (outputs/relais)
            if isinstance(data, dict):
                state = data.get("state")
                esp_timestamp = data.get("timestamp")
                esp_us = data.get("us", 0)  # Microsecondes (0 par défaut)

                if state is None or esp_timestamp is None:
                    print(f"📨 Message de statut incomplet reçu pour {relay_name}: {payload}")
                    return

                state_str = "ON" if state == 1 else "OFF"
                print(f"📨 Statut reçu pour {relay_name}: {state_str} (ESP time: {esp_timestamp}.{esp_us:06d})")

                # Vérifier si une commande était en attente pour ce relais
                if relay_name in pending_commands:
                    command_info = pending_commands.pop(relay_name)
                    
                    if command_info['type'] == 'immediate':
                        send_time = command_info['time']
                        latency = (receipt_time - send_time) * 1000
                        print(f"   └── ⏱️  Latence de la commande immédiate: {latency:.3f} ms")
                    
                    elif command_info['type'] == 'scheduled':
                        exec_at_sec = command_info['exec_at_sec']
                        exec_at_us = command_info['exec_at_us']
                        
                        # Calculer le délai en microsecondes
                        expected_time_us = (exec_at_sec * 1000000) + exec_at_us
                        actual_time_us = (esp_timestamp * 1000000) + esp_us
                        delay_us = actual_time_us - expected_time_us
                        delay_ms = delay_us / 1000.0
                        
                        print(f"   └── 🗓️  Commande programmée exécutée:")
                        print(f"        - Heure demandée : {exec_at_sec}.{exec_at_us:06d}")
                        print(f"        - Heure exécution: {esp_timestamp}.{esp_us:06d}")
                        print(f"        - Décalage       : {delay_ms:.3f} ms ({delay_us} µs)")
            
            # Si c'est juste un nombre (inputs)
            elif isinstance(data, int):
                state_str = "HIGH" if data == 1 else "LOW"
                print(f"📨 Input {relay_name}: {state_str}")

        except (json.JSONDecodeError, KeyError):
            # Gérer les anciens messages ou les messages mal formés
            print(f"📨 Message (non-JSON ou mal formé) reçu: {topic} = {payload}")

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
def set_relay(client, relay_name, state, exec_at_sec=None, exec_at_us=None):
    """Active ou désactive un relais, immédiatement ou de manière programmée"""
    topic = f"{DEVICE_NAME}/control/{relay_name}/set"
    
    payload_data = {"state": 1 if state else 0}
    if exec_at_sec is not None:
        payload_data["exec_at"] = exec_at_sec
        payload_data["exec_at_us"] = exec_at_us if exec_at_us is not None else 0
    
    payload = json.dumps(payload_data)
    
    # Enregistrer les informations sur la commande pour le calcul de la latence/délai
    if exec_at_sec is not None:
        pending_commands[relay_name] = {
            'type': 'scheduled', 
            'exec_at_sec': exec_at_sec,
            'exec_at_us': exec_at_us if exec_at_us is not None else 0
        }
    else:
        pending_commands[relay_name] = {'type': 'immediate', 'time': time.time()}

    result = client.publish(topic, payload, qos=1)
    
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        action = "ON" if state else "OFF"
        if exec_at_sec is not None:
            exec_time_str = time.strftime('%H:%M:%S', time.localtime(exec_at_sec))
            exec_us = exec_at_us if exec_at_us is not None else 0
            print(f"✓ Commande programmée envoyée: {relay_name} -> {action} à {exec_time_str}.{exec_us:06d}")
        else:
            print(f"✓ Commande immédiate envoyée: {relay_name} -> {action}")
    else:
        print(f"✗ Erreur lors de l'envoi de la commande")
        # Si l'envoi échoue, retirer la commande des commandes en attente
        pending_commands.pop(relay_name, None)

def turn_on(client, relay_name):
    """Active un relais immédiatement"""
    set_relay(client, relay_name, True)

def turn_off(client, relay_name):
    """Désactive un relais immédiatement"""
    set_relay(client, relay_name, False)

def schedule_toggle(client, relay_name, delay_seconds=5):
    """Programme l'activation d'un relais dans le futur avec précision microseconde"""
    current_time = time.time()
    exec_time = current_time + delay_seconds
    
    exec_seconds = int(exec_time)
    exec_us = int((exec_time - exec_seconds) * 1000000)
    
    print(f"\n🗓️ Programmation de {relay_name} pour s'activer dans {delay_seconds} secondes...")
    exec_time_str = time.strftime('%H:%M:%S', time.localtime(exec_seconds))
    print(f"   Exécution prévue: {exec_time_str}.{exec_us:06d}")
    
    set_relay(client, relay_name, True, exec_at_sec=exec_seconds, exec_at_us=exec_us)


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
    print(f"{offset+3}. Activer {RELAY_NAMES[0]} dans 5 secondes")
    print(f"{offset+4}. Publier timestamp maintenant")
    print(f"{offset+5}. Mesurer la qualité de synchronisation")
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

def publish_time_now(client):
    """Publie le timestamp immédiatement avec précision microseconde"""
    if client.is_connected():
        current_time = time.time()
        seconds = int(current_time)
        microseconds = int((current_time - seconds) * 1000000)
        
        payload = json.dumps({
            "seconds": seconds,
            "us": microseconds
        })
        
        topic = "esp32/time/sync"
        client.publish(topic, payload, qos=1)
        
        time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(seconds))
        print(f"\u23f0 Timestamp publié manuellement: {seconds}.{microseconds:06d} ({time_str}.{microseconds:06d})")
    else:
        print("⚠ Client MQTT non connecté")

def measure_sync_quality(client):
    """Mesure la qualité de synchronisation en envoyant plusieurs pings"""
    if not client.is_connected():
        print("⚠ Client MQTT non connecté")
        return
    
    print("\n🔬 Mesure de la qualité de synchronisation...")
    print("Envoi de 10 pings pour mesurer la latence réseau...\n")
    
    # Nettoyer les anciens échantillons
    old_samples = latency_tracker['samples'].copy()
    latency_tracker['samples'].clear()
    
    # Envoyer 10 pings rapides
    for i in range(10):
        ping_id = f"measure_{int(time.time() * 1000000)}_{i}"
        latency_tracker['ping_times'][ping_id] = time.time()
        client.publish(f"{DEVICE_NAME}/ping", ping_id)
        time.sleep(0.05)  # 50ms entre chaque ping
    
    # Attendre les réponses
    print("Attente des réponses...")
    time.sleep(2)
    
    # Analyser les résultats
    if len(latency_tracker['samples']) > 0:
        rtts_ms = [rtt / 1000.0 for rtt in latency_tracker['samples']]
        avg_rtt = sum(rtts_ms) / len(rtts_ms)
        min_rtt = min(rtts_ms)
        max_rtt = max(rtts_ms)
        jitter = max_rtt - min_rtt
        latency_ms = avg_rtt / 2
        
        print(f"\n📊 Résultats ({len(rtts_ms)} échantillons):")
        print(f"  RTT moyen:    {avg_rtt:.3f} ms")
        print(f"  RTT min:      {min_rtt:.3f} ms")
        print(f"  RTT max:      {max_rtt:.3f} ms")
        print(f"  Jitter:       {jitter:.3f} ms")
        print(f"  Latence est.: {latency_ms:.3f} ms")
        
        print(f"\n🎯 Précision de synchronisation estimée: ±{latency_ms:.2f} ms")
        
        if latency_ms < 2:
            print("  ✅ Excellente qualité - précision sub-milliseconde possible")
        elif latency_ms < 5:
            print("  ✅ Très bonne qualité - précision de quelques millisecondes")
        elif latency_ms < 10:
            print("  ✓ Bonne qualité - précision ~10ms")
        elif latency_ms < 20:
            print("  ⚠️  Qualité moyenne - précision ~20ms")
        else:
            print("  ❌ Faible qualité - vérifier le réseau")
    else:
        print("\n❌ Aucune réponse reçue. Vérifiez la connexion MQTT.")
        # Restaurer les anciens échantillons
        latency_tracker['samples'] = old_samples

def publish_time(client):
    """Publie le timestamp actuel avec précision microseconde et mesure la latence"""
    while True:
        if client.is_connected():
            current_loop_time = time.time()
            
            # Mesurer la latence toutes les 30 secondes
            if current_loop_time - latency_tracker['last_measurement'] > 30:
                # Envoyer un ping pour mesurer la latence
                ping_id = str(int(current_loop_time * 1000000))
                latency_tracker['ping_times'][ping_id] = current_loop_time
                ping_topic = f"{DEVICE_NAME}/ping"
                client.publish(ping_topic, ping_id)
                latency_tracker['last_measurement'] = current_loop_time
            
            # Obtenir le temps avec microsecondes
            current_time = time.time()
            seconds = int(current_time)
            microseconds = int((current_time - seconds) * 1000000)
            
            # Créer le payload JSON avec microsecondes
            payload = json.dumps({
                "seconds": seconds,
                "us": microseconds
            })
            
            topic = "esp32/time/sync"  # Topic commun à tous les ESP32
            client.publish(topic, payload, qos=1)  # QoS 1 pour garantir la livraison
            
            time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(seconds))
            
            # Afficher avec info de latence si disponible
            if latency_tracker['avg_latency_us'] > 0:
                print(f"⏰ Sync: {seconds}.{microseconds:06d} | Latence: ±{latency_tracker['avg_latency_us']/1000:.2f}ms")
            else:
                print(f"⏰ Sync: {seconds}.{microseconds:06d} (mesure latence en cours...)")
        
        time.sleep(10)  # Synchroniser toutes les 10 secondes

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
    global DEVICE_NAME
    
    print("="*60)
    print("ESP32 IO Controller - Script de Test MQTT")
    print("="*60)
    
    local_ip = get_local_ip()
    
    # Vérifier si le broker est en ligne, sinon le redémarrer
    check_and_restart_mosquitto(local_ip, MQTT_PORT)

    print(f"\n✅ L'adresse IP de ce PC est: {local_ip}")
    
    # Demander le nom du device à contrôler
    print("\n" + "="*60)
    print("SÉLECTION DE L'APPAREIL")
    print("="*60)
    device_input = input(f"Nom de l'appareil ESP32 (par défaut: '{DEVICE_NAME}'): ").strip()
    if device_input:
        DEVICE_NAME = device_input
        print(f"✓ Appareil sélectionné: {DEVICE_NAME}")
    else:
        print(f"✓ Utilisation de l'appareil par défaut: {DEVICE_NAME}")
    
    print("\n" + "="*60)
    print("📋 CONFIGURATION REQUISE POUR L'ESP32")
    print("="*60)
    print("Assurez-vous que votre ESP32 est configuré avec les paramètres suivants:")
    print(f"  - MQTT Server: \"{local_ip}\"")
    print(f"  - MQTT Port:   {MQTT_PORT}")
    print(f"  - Nom appareil: \"{DEVICE_NAME}\"") 
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

                # Commande programmée
                elif choice == num_relays*2 + 3:
                    schedule_toggle(client, RELAY_NAMES[0], delay_seconds=5)
                
                # Publier timestamp maintenant
                elif choice == num_relays*2 + 4:
                    publish_time_now(client)
                
                # Mesurer la qualité de synchronisation
                elif choice == num_relays*2 + 5:
                    measure_sync_quality(client)
                
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
