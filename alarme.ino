
#include <WiFi.h>
#include <PubSubClient.h>

// ===============================
// WIFI
// ===============================

const char* ssid = "iot";
const char* password = "iotisis;";

// ===============================
// MQTT
// ===============================

const char* mqtt_server = "172.18.32.43";

WiFiClient espClient;
PubSubClient client(espClient);

// ===============================
// LED ET BOUTON
// ===============================

#define LED_PIN 25
#define BUTTON_PIN 26

// Topics MQTT
const char* topicLED = "esp32/output";
const char* topicButton = "esp32/button";

// Gestion du bouton
int lastButtonState = HIGH;
int buttonState = HIGH;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ===============================
// RECEPTION MQTT
// ===============================

void callback(char* topic, byte* payload, unsigned int length) {

  String message = "";

  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message recu : ");
  Serial.println(message);

  if (String(topic) == topicLED) {

    if (message == "1") {
      digitalWrite(LED_PIN, HIGH);
      Serial.println("LED ALLUMEE");
    }

    else if (message == "0") {
      digitalWrite(LED_PIN, LOW);
      Serial.println("LED ETEINTE");
    }
  }
}

// ===============================
// CONNEXION WIFI
// ===============================

void setupWiFi() {

  Serial.print("Connexion WiFi");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connecte !");
  Serial.println(WiFi.localIP());
}

// ===============================
// CONNEXION MQTT
// ===============================

void reconnect() {

  while (!client.connected()) {

    Serial.print("Connexion MQTT...");

    if (client.connect("ESP32_NUMERO_3")) {

      Serial.println("Connecte !");

      client.subscribe(topicLED);

      Serial.println("Abonne au topic esp32/output");

    } else {

      Serial.print("Echec MQTT, code : ");
      Serial.println(client.state());

      delay(2000);
    }
  }
}

// ===============================
// SETUP
// ===============================

void setup() {

  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, LOW);

  setupWiFi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

// ===============================
// LOOP
// ===============================

void loop() {

  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  // Lecture du bouton
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {

    if (reading != buttonState) {

      buttonState = reading;

      if (buttonState == LOW) {

        client.publish(topicButton, "1");
        Serial.println("Bouton appuye");

      } else {

        client.publish(topicButton, "0");
        Serial.println("Bouton relache");
      }
    }
  }

  lastButtonState = reading;
}
