#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"

extern AsyncWebServer server;
extern Config config;

// Page HTML principale
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Contrôle Volet Roulant</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: Arial, sans-serif; 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            padding: 20px;
            min-height: 100vh;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            border-radius: 15px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            overflow: hidden;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }
        .header h1 { font-size: 2em; margin-bottom: 10px; }
        .tabs {
            display: flex;
            background: #f5f5f5;
            border-bottom: 2px solid #ddd;
        }
        .tab {
            flex: 1;
            padding: 15px;
            text-align: center;
            cursor: pointer;
            background: #f5f5f5;
            border: none;
            font-size: 16px;
            transition: all 0.3s;
        }
        .tab:hover { background: #e0e0e0; }
        .tab.active {
            background: white;
            border-bottom: 3px solid #667eea;
            font-weight: bold;
        }
        .content {
            padding: 30px;
        }
        .tab-content {
            display: none;
        }
        .tab-content.active {
            display: block;
        }
        .control-panel {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .btn {
            padding: 20px;
            font-size: 18px;
            border: none;
            border-radius: 10px;
            cursor: pointer;
            transition: all 0.3s;
            color: white;
            font-weight: bold;
            text-transform: uppercase;
        }
        .btn-open {
            background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%);
        }
        .btn-close {
            background: linear-gradient(135deg, #eb3349 0%, #f45c43 100%);
        }
        .btn-stop {
            background: linear-gradient(135deg, #757F9A 0%, #D7DDE8 100%);
        }
        .btn:hover {
            transform: translateY(-3px);
            box-shadow: 0 10px 20px rgba(0,0,0,0.2);
        }
        .form-group {
            margin-bottom: 20px;
        }
        .form-group label {
            display: block;
            margin-bottom: 5px;
            font-weight: bold;
            color: #333;
        }
        .form-group input, .form-group select {
            width: 100%;
            padding: 12px;
            border: 2px solid #ddd;
            border-radius: 8px;
            font-size: 16px;
        }
        .form-group input:focus {
            outline: none;
            border-color: #667eea;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            margin-top: 20px;
        }
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        th {
            background: #667eea;
            color: white;
            font-weight: bold;
        }
        tr:hover {
            background: #f5f5f5;
        }
        .badge {
            padding: 5px 10px;
            border-radius: 15px;
            font-size: 12px;
            font-weight: bold;
        }
        .badge-success {
            background: #38ef7d;
            color: white;
        }
        .badge-danger {
            background: #f45c43;
            color: white;
        }
        .status-box {
            background: #f8f9fa;
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
            border-left: 5px solid #667eea;
        }
        .status-item {
            display: flex;
            justify-content: space-between;
            padding: 10px 0;
            border-bottom: 1px solid #ddd;
        }
        .status-item:last-child {
            border-bottom: none;
        }
        .btn-small {
            padding: 8px 15px;
            font-size: 14px;
            border-radius: 5px;
            border: none;
            cursor: pointer;
            margin: 2px;
        }
        .btn-delete {
            background: #f45c43;
            color: white;
        }
        .btn-add {
            background: #38ef7d;
            color: white;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🏠 Contrôle Volet Roulant ESP32</h1>
            <p>Gestion intelligente avec Wiegand, RFID et Empreinte</p>
        </div>
        
        <div class="tabs">
            <button class="tab active" onclick="switchTab('control')">Contrôle</button>
            <button class="tab" onclick="switchTab('codes')">Codes d'Accès</button>
            <button class="tab" onclick="switchTab('logs')">Historique</button>
            <button class="tab" onclick="switchTab('config')">Configuration</button>
            <button class="tab" onclick="switchTab('update')">Mise à Jour</button>
        </div>
        
        <div class="content">
            <!-- TAB CONTROLE -->
            <div id="control" class="tab-content active">
                <h2>Contrôle Manuel</h2>
                <div class="control-panel">
                    <button class="btn btn-open" onclick="controlRelay('open')">⬆️ Ouvrir</button>
                    <button class="btn btn-close" onclick="controlRelay('close')">⬇️ Fermer</button>
                    <button class="btn btn-stop" onclick="controlRelay('stop')">⏹️ Stop</button>
                </div>
                
                <div class="status-box">
                    <h3>État du Système</h3>
                    <div class="status-item">
                        <span>WiFi:</span>
                        <span id="wifi-status">Connecté</span>
                    </div>
                    <div class="status-item">
                        <span>MQTT:</span>
                        <span id="mqtt-status">...</span>
                    </div>
                    <div class="status-item">
                        <span>Barrière Photo:</span>
                        <span id="barrier-status">...</span>
                    </div>
                    <div class="status-item">
                        <span>Relais:</span>
                        <span id="relay-status">Inactif</span>
                    </div>
                </div>
            </div>
            
            <!-- TAB CODES -->
            <div id="codes" class="tab-content">
                <h2>Codes d'Accès</h2>
                <button class="btn btn-add" onclick="showAddCodeForm()">+ Ajouter un Code</button>
                
                <div id="add-code-form" style="display:none; margin-top: 20px; padding: 20px; background: #f8f9fa; border-radius: 10px;">
                    <h3>Nouveau Code</h3>
                    <div class="form-group">
                        <label>Code (numérique):</label>
                        <input type="number" id="new-code" placeholder="Ex: 1234">
                    </div>
                    <div class="form-group">
                        <label>Type:</label>
                        <select id="new-type">
                            <option value="0">Wiegand/Clavier</option>
                            <option value="1">RFID</option>
                            <option value="2">Empreinte</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>Nom:</label>
                        <input type="text" id="new-name" placeholder="Ex: Utilisateur 1">
                    </div>
                    <button class="btn-small btn-add" onclick="addCode()">Enregistrer</button>
                    <button class="btn-small" onclick="hideAddCodeForm()">Annuler</button>
                </div>
                
                <table id="codes-table">
                    <thead>
                        <tr>
                            <th>Code</th>
                            <th>Type</th>
                            <th>Nom</th>
                            <th>Statut</th>
                            <th>Actions</th>
                        </tr>
                    </thead>
                    <tbody id="codes-tbody">
                        <tr><td colspan="5">Chargement...</td></tr>
                    </tbody>
                </table>
            </div>
            
            <!-- TAB LOGS -->
            <div id="logs" class="tab-content">
                <h2>Historique des Accès</h2>
                <button class="btn-small btn-add" onclick="loadLogs()">🔄 Actualiser</button>
                <table id="logs-table">
                    <thead>
                        <tr>
                            <th>Horodatage</th>
                            <th>Code</th>
                            <th>Type</th>
                            <th>Résultat</th>
                        </tr>
                    </thead>
                    <tbody id="logs-tbody">
                        <tr><td colspan="4">Chargement...</td></tr>
                    </tbody>
                </table>
            </div>
            
            <!-- TAB CONFIG -->
            <div id="config" class="tab-content">
                <h2>Configuration</h2>
                
                <h3>Relais</h3>
                <div class="form-group">
                    <label>Durée d'activation (ms):</label>
                    <input type="number" id="relay-duration" value="5000">
                </div>
                <div class="form-group">
                    <label>Barrière photoélectrique:</label>
                    <select id="photo-enabled">
                        <option value="1">Activée</option>
                        <option value="0">Désactivée</option>
                    </select>
                </div>
                
                <h3 style="margin-top: 30px;">MQTT</h3>
                <div class="form-group">
                    <label>Serveur:</label>
                    <input type="text" id="mqtt-server" placeholder="mqtt.example.com">
                </div>
                <div class="form-group">
                    <label>Port:</label>
                    <input type="number" id="mqtt-port" value="1883">
                </div>
                <div class="form-group">
                    <label>Utilisateur:</label>
                    <input type="text" id="mqtt-user">
                </div>
                <div class="form-group">
                    <label>Mot de passe:</label>
                    <input type="password" id="mqtt-password">
                </div>
                <div class="form-group">
                    <label>Topic:</label>
                    <input type="text" id="mqtt-topic" value="roller">
                </div>
                
                <h3 style="margin-top: 30px;">Sécurité</h3>
                <div class="form-group">
                    <label>Mot de passe admin:</label>
                    <input type="password" id="admin-password">
                </div>
                
                <button class="btn btn-add" onclick="saveConfig()">💾 Enregistrer Configuration</button>
            </div>
            
            <!-- TAB UPDATE -->
            <div id="update" class="tab-content">
                <h2>Mise à Jour OTA</h2>
                <p>Accédez à la page de mise à jour:</p>
                <a href="/update" target="_blank">
                    <button class="btn btn-open">📡 Ouvrir Interface OTA</button>
                </a>
            </div>
        </div>
    </div>

    <script>
        function switchTab(tabName) {
            const tabs = document.querySelectorAll('.tab');
            const contents = document.querySelectorAll('.tab-content');
            
            tabs.forEach(tab => tab.classList.remove('active'));
            contents.forEach(content => content.classList.remove('active'));
            
            event.target.classList.add('active');
            document.getElementById(tabName).classList.add('active');
            
            if (tabName === 'codes') loadCodes();
            if (tabName === 'logs') loadLogs();
            if (tabName === 'config') loadConfig();
        }
        
        function controlRelay(action) {
            fetch('/api/relay', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({action: action})
            })
            .then(r => r.json())
            .then(data => {
                alert(data.message || 'Commande envoyée');
                document.getElementById('relay-status').textContent = action === 'stop' ? 'Inactif' : action;
            });
        }
        
        function loadCodes() {
            fetch('/api/codes')
            .then(r => r.json())
            .then(data => {
                const tbody = document.getElementById('codes-tbody');
                tbody.innerHTML = '';
                data.codes.forEach((code, idx) => {
                    const types = ['Wiegand', 'RFID', 'Empreinte'];
                    tbody.innerHTML += `
                        <tr>
                            <td>${code.code}</td>
                            <td>${types[code.type]}</td>
                            <td>${code.name}</td>
                            <td><span class="badge badge-success">${code.active ? 'Actif' : 'Inactif'}</span></td>
                            <td><button class="btn-small btn-delete" onclick="deleteCode(${idx})">Supprimer</button></td>
                        </tr>
                    `;
                });
            });
        }
        
        function loadLogs() {
            fetch('/api/logs')
            .then(r => r.json())
            .then(data => {
                const tbody = document.getElementById('logs-tbody');
                tbody.innerHTML = '';
                data.logs.forEach(log => {
                    const types = ['Wiegand', 'RFID', 'Empreinte'];
                    const badge = log.granted ? 'badge-success' : 'badge-danger';
                    const result = log.granted ? 'Accordé' : 'Refusé';
                    tbody.innerHTML += `
                        <tr>
                            <td>${new Date(log.timestamp).toLocaleString()}</td>
                            <td>${log.code}</td>
                            <td>${types[log.type]}</td>
                            <td><span class="badge ${badge}">${result}</span></td>
                        </tr>
                    `;
                });
            });
        }
        
        function loadConfig() {
            fetch('/api/config')
            .then(r => r.json())
            .then(data => {
                document.getElementById('relay-duration').value = data.relayDuration;
                document.getElementById('photo-enabled').value = data.photoEnabled ? '1' : '0';
                document.getElementById('mqtt-server').value = data.mqttServer;
                document.getElementById('mqtt-port').value = data.mqttPort;
                document.getElementById('mqtt-user').value = data.mqttUser;
                document.getElementById('mqtt-topic').value = data.mqttTopic;
            });
        }
        
        function saveConfig() {
            const config = {
                relayDuration: parseInt(document.getElementById('relay-duration').value),
                photoEnabled: document.getElementById('photo-enabled').value === '1',
                mqttServer: document.getElementById('mqtt-server').value,
                mqttPort: parseInt(document.getElementById('mqtt-port').value),
                mqttUser: document.getElementById('mqtt-user').value,
                mqttPassword: document.getElementById('mqtt-password').value,
                mqttTopic: document.getElementById('mqtt-topic').value,
                adminPassword: document.getElementById('admin-password').value
            };
            
            fetch('/api/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(config)
            })
            .then(r => r.json())
            .then(data => alert(data.message || 'Configuration enregistrée'));
        }
        
        function showAddCodeForm() {
            document.getElementById('add-code-form').style.display = 'block';
        }
        
        function hideAddCodeForm() {
            document.getElementById('add-code-form').style.display = 'none';
            // Réinitialiser le formulaire
            document.getElementById('new-code').value = '';
            document.getElementById('new-type').value = '0';
            document.getElementById('new-name').value = '';
        }
        
        function addCode() {
            const codeValue = parseInt(document.getElementById('new-code').value);
            const nameValue = document.getElementById('new-name').value;
            
            // Validation
            if (!codeValue || isNaN(codeValue)) {
                alert('Code invalide');
                return;
            }
            
            if (!nameValue || nameValue.trim() === '') {
                alert('Nom requis');
                return;
            }
            
            const code = {
                code: codeValue,
                type: parseInt(document.getElementById('new-type').value),
                name: nameValue.trim(),
                active: true
            };
            
            fetch('/api/codes', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(code)
            })
            .then(r => r.json())
            .then(data => {
                alert(data.message || data.error || 'Code ajouté');
                hideAddCodeForm();
                loadCodes();
            })
            .catch(err => {
                alert('Erreur lors de l\'ajout: ' + err);
            });
        }
        
        function deleteCode(index) {
            if (!confirm('Supprimer ce code?')) return;
            
            console.log('Deleting code at index:', index);
            
            fetch('/api/codes/delete?index=' + index)
            .then(response => {
                console.log('Response status:', response.status);
                return response.json().then(data => ({status: response.status, body: data}));
            })
            .then(result => {
                console.log('Response:', result);
                if (result.status === 200) {
                    alert(result.body.message || 'Code supprimé');
                    // Attendre un peu avant de recharger pour être sûr que la NVS est sauvegardée
                    setTimeout(() => loadCodes(), 100);
                } else {
                    alert('Erreur: ' + (result.body.error || 'Erreur inconnue'));
                }
            })
            .catch(err => {
                console.error('Delete error:', err);
                alert('Erreur lors de la suppression: ' + err);
            });
        }
        
        // Charger les données au démarrage
        loadCodes();
        setInterval(() => {
            fetch('/api/status').then(r => r.json()).then(data => {
                document.getElementById('mqtt-status').textContent = data.mqtt ? 'Connecté' : 'Déconnecté';
                document.getElementById('barrier-status').textContent = data.barrier ? 'OK' : 'Coupée';
            });
        }, 2000);
    </script>
</body>
</html>
)rawliteral";

void setupWebServer();

#endif
