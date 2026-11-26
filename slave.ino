#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <DHT.h>
#include "DFRobot_BloodOxygen_S.h"



const char* ssid = "iot";
const char* password = "iotisis;";

const char* mqtt_server = "172.18.32.43";

WiFiClient espClient;
PubSubClient client(espClient);



#define DHTPIN 5
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);



#define I2C_ADDRESS 0x57

TwoWire I2C_2 = TwoWire(1);

DFRobot_BloodOxygen_S_I2C MAX30102(&I2C_2, I2C_ADDRESS);

bool max30102OK = false;   // le capteur repond-il sur son bus I2C ?
bool dhtOK = true;         // la derniere lecture du DHT11 a-t-elle reussi ?



#define I2C_DEV_ADDR 0x55



const int ledPin = 4;



#define ERR_NONE              0
#define ERR_WIFI_LOST         1
#define ERR_SENSOR_DISCONNECT 2
#define ERR_SENSOR_READ       3
#define ERR_TEMP_LIMIT        4
#define ERR_LOW_VOLTAGE       5
#define ERR_SERIAL_LOST       6
#define ERR_LOW_MEMORY        7
#define ERR_I2C               8
#define ERR_WATCHDOG          9
#define ERR_UNKNOWN          10
#define ERR_MQTT_LOST        11   


volatile uint8_t errorCode = ERR_NONE;



unsigned long previousSensorMillis = 0;
unsigned long previousMqttMillis = 0;
unsigned long previousErrorMillis = 0;
unsigned long previousReconnectMillis = 0;
unsigned long previousMaxMillis = 0;          

const unsigned long sensorInterval = 4000;    
const unsigned long mqttInterval = 2000;
const unsigned long errorInterval = 1000;      
const unsigned long reconnectInterval = 5000;  
const unsigned long maxInterval = 1000;        

// =====================================
// VARIABLES CAPTEURS
// =====================================

float temperature = NAN;
float humidity = NAN;

int spo2 = -1;                 
int heartRate = -1;
float boardTemperature = NAN;


const int MAX_INVALID = 4;
int invalidCount = MAX_INVALID;   // au demarrage : pas de doigt

// =====================================
// DECLARATIONS
// =====================================

void setup_wifi();
void reconnect();
void callback(char* topic, byte* message, unsigned int length);

void readSensors();
void readMax30102();
void publishSensors();
void updateError();
bool max30102Present();

// =====================================
// INITIALISATION
// =====================================

void setup() {

  Serial.begin(115200);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // DHT11
  dht.begin();

  // MAX30102 : bus I2C secondaire
  I2C_2.begin(25, 26, 100000);

  if (MAX30102.begin()) {
    Serial.println("MAX30102 initialise avec succes");
    MAX30102.sensorStartCollect();
    max30102OK = true;
  } else {
    Serial.println("Erreur initialisation MAX30102");
    max30102OK = false;
  }

  // I2C SLAVE (declare AVANT le Wifi pour que le Master puisse deja nous interroger)
  Wire.begin((uint8_t)I2C_DEV_ADDR, 21, 22, 100000);

  Wire.onReceive([](int len) {
    Serial.print("Commande recue du Master : ");
    while (Wire.available()) {
      Serial.write(Wire.read());
    }
    Serial.println();
  });

  Wire.onRequest([]() {
    Wire.write((uint8_t)errorCode);
  });

  Serial.println("ESP32 I2C Slave initialise");

  // WIFI
  setup_wifi();

  // MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  Serial.println("Systeme initialise !");
}

// =====================================
// CONNEXION WIFI
// =====================================

void setup_wifi() {

  WiFi.begin(ssid, password);

  Serial.print("Connexion WiFi");

  // Tant que le Wifi n'est pas connecte, le Master voit l'erreur 1
  errorCode = ERR_WIFI_LOST;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connecte");

  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}

// =====================================
// RECEPTION MQTT
// =====================================

void callback(char* topic, byte* message, unsigned int length) {

  String messageTemp = "";

  for (unsigned int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }

  Serial.print("Message MQTT : ");
  Serial.println(messageTemp);

  if (String(topic) == "esp32/output") {

    if (messageTemp == "1") {
      digitalWrite(ledPin, HIGH);
    }
    else if (messageTemp == "0") {
      digitalWrite(ledPin, LOW);
    }
  }
}

// =====================================
// CONNEXION MQTT
// =====================================

void reconnect() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!client.connected()) {

    Serial.print("Connexion MQTT...");

    if (client.connect("ESP32Client_DHT_MAX30102")) {
      Serial.println("connecte");
      client.subscribe("esp32/output");
    } else {
      Serial.print("Echec MQTT, code : ");
      Serial.println(client.state());
    }
  }
}

// =====================================
// LE CAPTEUR MAX30102 REPOND-IL SUR SON BUS ?
// =====================================

bool max30102Present() {
  I2C_2.beginTransmission(I2C_ADDRESS);
  return (I2C_2.endTransmission() == 0);   // 0 = le capteur a repondu
}

// =====================================
// LECTURE DU DHT11 (toutes les 4 s)
// =====================================

void readSensors() {

  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h) && !isnan(t)) {

    dhtOK = true;
    humidity = h;
    temperature = t;

    Serial.print("Humidite : ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("Temperature : ");
    Serial.print(temperature);
    Serial.println(" C");

  } else {

    dhtOK = false;
    humidity = NAN;      // on n'envoie plus de vieilles valeurs
    temperature = NAN;

    Serial.println("Erreur lecture DHT11");
  }
}

// =====================================
// LECTURE DU MAX30102 (chaque seconde)

// =====================================
//  - une mesure n'est acceptee que si SpO2 ET frequence cardiaque
//    sont valides en meme temps
//  - une mesure invalide isolee est ignoree (on garde la derniere bonne valeur)
//  - on ne passe a -1 qu'apres MAX_INVALID lectures invalides consecutives

void readMax30102() {

  if (!max30102OK) {
    spo2 = -1;
    heartRate = -1;
    return;
  }

  MAX30102.getHeartbeatSPO2();

  int s = MAX30102._sHeartbeatSPO2.SPO2;
  int h = MAX30102._sHeartbeatSPO2.Heartbeat;

  if (s > 0 && s <= 100 && h > 0 && h < 220) {

    
    spo2 = s;
    heartRate = h;
    invalidCount = 0;

  } else {

    if (invalidCount < MAX_INVALID) {
      invalidCount++;
    }

    // Tolerance depassee : le doigt est considere absent
    if (invalidCount >= MAX_INVALID) {
      spo2 = -1;
      heartRate = -1;
    }
  }

  boardTemperature = MAX30102.getTemperature_C();

  Serial.print("SpO2 : ");
  Serial.print(spo2);
  Serial.print(" % | FC : ");
  Serial.print(heartRate);
  Serial.print(" BPM | lectures invalides : ");
  Serial.println(invalidCount);
}

// =====================================
// PUBLICATION MQTT
// =====================================

void publishSensors() {

  if (!client.connected()) {
    return;
  }

  char buffer[20];

  // TEMPERATURE DHT11
  if (!isnan(temperature)) {
    snprintf(buffer, sizeof(buffer), "%.2f", temperature);
    client.publish("esp32/DHT11/temperature", buffer);
  }

  // HUMIDITE DHT11
  if (!isnan(humidity)) {
    snprintf(buffer, sizeof(buffer), "%.2f", humidity);
    client.publish("esp32/DHT11/humidity", buffer);
  }

  // MAX30102
  if (max30102OK) {

    // SpO2 et frequence cardiaque publiees ensemble (valides ou -1 tous les deux)
    snprintf(buffer, sizeof(buffer), "%d", spo2);
    client.publish("esp32/MAX30102/spo2", buffer);

    snprintf(buffer, sizeof(buffer), "%d", heartRate);
    client.publish("esp32/MAX30102/heartRate", buffer);

    if (!isnan(boardTemperature)) {
      snprintf(buffer, sizeof(buffer), "%.2f", boardTemperature);
      client.publish("esp32/MAX30102/temperature", buffer);
    }

  } else {

    // Capteur deconnecte : envoyer une valeur invalide
    client.publish("esp32/MAX30102/spo2", "-1");
    client.publish("esp32/MAX30102/heartRate", "-1");
  }

 
  snprintf(buffer, sizeof(buffer), "%d", errorCode);
  client.publish("esp32/error", buffer);

  Serial.println("Donnees envoyees vers MQTT !");
}

// =====================================
// GESTION DES ERREURS (appelee chaque seconde)


void updateError() {

 
  bool present = max30102Present();

  if (!present) {
    if (max30102OK) {
      Serial.println("!!! MAX30102 deconnecte !!!");
    }
    max30102OK = false;
  }
  else if (!max30102OK) {
     
    if (MAX30102.begin()) {
      MAX30102.sensorStartCollect();
      max30102OK = true;
      invalidCount = MAX_INVALID;   
      Serial.println("MAX30102 reconnecte et redemarre");
    }
  }

  // 2) Code erreur (ordre de priorite)
  uint8_t newCode;

  if (WiFi.status() != WL_CONNECTED)  newCode = ERR_WIFI_LOST;
  else if (!client.connected())       newCode = ERR_MQTT_LOST;
  else if (!max30102OK)               newCode = ERR_SENSOR_DISCONNECT;
  else if (!dhtOK)                    newCode = ERR_SENSOR_READ;
  else                                newCode = ERR_NONE;

  // 3) On affiche seulement quand le code change
  if (newCode != errorCode) {
    Serial.print(">>> Nouveau code erreur : ");
    Serial.println(newCode);
  }
  errorCode = newCode;
}

// =====================================
// BOUCLE PRINCIPALE
// =====================================

void loop() {

  unsigned long now = millis();

  // Verification WIFI / MQTT : une tentative toutes les 5 secondes (sans bloquer le reste)
  if (now - previousReconnectMillis >= reconnectInterval) {

    previousReconnectMillis = now;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi deconnecte !");
      WiFi.reconnect();
    }
    else if (!client.connected()) {
      reconnect();
    }
  }

  client.loop();

  // Mise a jour du code erreur chaque seconde
  if (now - previousErrorMillis >= errorInterval) {
    previousErrorMillis = now;
    updateError();
  }

  // CORRECTION : lecture du MAX30102 chaque seconde
  if (now - previousMaxMillis >= maxInterval) {
    previousMaxMillis = now;
    readMax30102();
  }

  // Lecture DHT11 + envoi MQTT toutes les 4 secondes
  if (now - previousSensorMillis >= sensorInterval) {
    previousSensorMillis = now;
    readSensors();
    publishSensors();
  }

}