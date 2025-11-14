#!/usr/bin/env python3
"""
Broker MQTT simple pour tester l'ESP32
Utilise mosquitto embarqué ou un broker Python simple
"""

import subprocess
import sys
import os
import socket

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

def check_mosquitto_installed():
    """Vérifie si Mosquitto est installé"""
    try:
        result = subprocess.run(['mosquitto', '-h'], 
                              capture_output=True, 
                              text=True, 
                              timeout=2)
        return True
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return False

def start_mosquitto():
    """Démarre Mosquitto en mode simple"""
    local_ip = get_local_ip()
    
    print("="*60)
    print("BROKER MQTT MOSQUITTO")
    print("="*60)
    print(f"Adresse IP locale: {local_ip}")
    print(f"Port MQTT: 1883")
    print("\n📋 Configuration ESP32:")
    print(f"   MQTT Server: {local_ip}")
    print(f"   MQTT Port: 1883")
    print("\n⚠️  Pour arrêter le broker: Ctrl+C")
    print("="*60 + "\n")
    
    try:
        # Démarrer mosquitto avec configuration minimale
        # -v: verbose, -p: port
        subprocess.run(['mosquitto', '-v', '-p', '1883'])
    except KeyboardInterrupt:
        print("\n\n👋 Arrêt du broker MQTT")
    except Exception as e:
        print(f"❌ Erreur: {e}")

def install_mosquitto_instructions():
    """Affiche les instructions d'installation de Mosquitto"""
    print("="*60)
    print("INSTALLATION DE MOSQUITTO")
    print("="*60)
    print("\n🪟 Windows:")
    print("   1. Télécharger: https://mosquitto.org/download/")
    print("   2. Installer l'exécutable")
    print("   3. Ajouter au PATH: C:\\Program Files\\mosquitto")
    print("\n   OU avec Chocolatey:")
    print("   choco install mosquitto")
    print("\n🐧 Linux:")
    print("   sudo apt-get install mosquitto")
    print("\n🍎 macOS:")
    print("   brew install mosquitto")
    print("\n" + "="*60)
    print("\n💡 Alternative: Utiliser un broker cloud gratuit:")
    print("   - test.mosquitto.org (port 1883)")
    print("   - broker.hivemq.com (port 1883)")
    print("="*60)

def main():
    """Fonction principale"""
    print("\n🚀 Démarrage du broker MQTT...\n")
    
    if check_mosquitto_installed():
        start_mosquitto()
    else:
        print("❌ Mosquitto n'est pas installé sur ce système\n")
        install_mosquitto_instructions()
        
        print("\n\n📦 ALTERNATIVE: Utiliser un broker Python simple")
        print("Voulez-vous installer 'hbmqtt' (broker Python) ? (o/n): ", end="")
        
        try:
            choice = input().lower()
            if choice == 'o':
                print("\nInstallation de hbmqtt...")
                subprocess.run([sys.executable, '-m', 'pip', 'install', 'hbmqtt'])
                print("\n✓ Installation terminée")
                print("Relancez ce script pour démarrer le broker")
            else:
                print("\n💡 Utilisez un broker cloud ou installez Mosquitto manuellement")
        except KeyboardInterrupt:
            print("\n\nAnnulé")

if __name__ == "__main__":
    main()
