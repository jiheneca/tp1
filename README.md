# Projet IoT – Surveillance Patient

Ce dépôt contient les travaux pratiques réalisés ainsi que le projet IoT de surveillance d'un patient.

## Organisation du dépôt

### 1. `sketch_nov10a/`
Ce dossier contient les fichiers des **TP1 et TP2**.

### 2. `alarme.ino`
Fichier Arduino dédié à la gestion des alarmes du système.

### 3. `master.ino`
Code de l'ESP32 Master, chargé de la communication avec l'ESP32 Slave et de la gestion des données.

### 4. `slave.ino`
Code de l'ESP32 Slave, chargé de la lecture des capteurs (MAX30102 et DHT11) et de la transmission des données.

### 5. `flows-nodered.json`
Fichier contenant les flows Node-RED utilisés pour la visualisation des données, la surveillance du patient et la gestion des alertes.

## Technologies utilisées
- ESP32
- Arduino IDE
- Communication I2C
- MQTT
- Node-RED
- Capteurs MAX30102 et DHT11

