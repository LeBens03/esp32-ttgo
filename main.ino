#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <TFT_eSPI.h>

// Configuration WiFi (fichier séparé non versionné)
#include "config_wifi.h"

// ==================== CONFIGURATION MATÉRIELLE ====================

// Pin de la thermistance (ADC)
const int thermistorPin = 36;

// Paramètres de la thermistance NTC
const float nominalResistance = 10000.0;  // Résistance à 25°C (10kΩ)
const float nominalTemp = 25.0;            // Température nominale (°C)
const float beta = 3950.0;                 // Coefficient β de la thermistance
const float seriesResistor = 10000.0;      // Résistance en série (1kΩ)

// ==================== OBJETS GLOBAUX ====================

WebServer server(80);  // Serveur HTTP sur port 80
TFT_eSPI tft = TFT_eSPI();  // Écran TFT

// ==================== FONCTIONS DE LECTURE CAPTEUR ====================

/**
 * Lecture de la température via thermistance NTC
 * Utilise l'équation de Steinhart-Hart simplifiée (équation β)
 * 
 * @return Température en degrés Celsius
 */
float readTemperature() {
  // Lecture de la valeur analogique (0-4095 pour ESP32)
  int analogValue = analogRead(thermistorPin);
  
  // Conversion en tension (0-3.3V)
  float voltage = analogValue * 3.3 / 4095.0;
  
  // Calcul de la résistance de la thermistance via diviseur de tension
  // R_thermistor = (V_supply - V_out) / V_out * R_series
  float resistance = (3.3 - voltage) / voltage * seriesResistor;
  
  // Équation β pour calculer la température en Kelvin
  // 1/T = 1/T0 + (1/β) * ln(R/R0)
  float temperatureK = 1.0 / (
    1.0 / (nominalTemp + 273.15) + 
    (1.0 / beta) * log(resistance / nominalResistance)
  );
  
  // Conversion en Celsius
  float temperatureC = temperatureK - 273.15;
  
  return temperatureC;
}

// ==================== GESTIONNAIRES HTTP (HANDLERS) ====================

/**
 * Handler pour la route racine "/"
 * Test de connectivité basique
 */
void handleRoot() {
  server.send(200, "text/plain", "Hello from ESP32 TTGO T-Display!");
}

/**
 * Handler pour la route "/temperature"
 * Retourne la température actuelle au format JSON
 */
void handleTemperature() {
  float tempC = readTemperature();
  
  // Construction de la réponse JSON
  String json = "{\"temperature\":" + String(tempC, 1) + ",\"unit\":\"celsius\"}";
  
  server.send(200, "application/json", json);
  
  // Log dans le moniteur série
  Serial.println("GET /temperature - Temp: " + String(tempC, 1) + "°C");
}

/**
 * Handler pour route non trouvée (404)
 */
void handleNotFound() {
  String message = "Route non trouvée\n\n";
  message += "URI: " + server.uri() + "\n";
  message += "Méthode: " + (server.method() == HTTP_GET ? "GET" : "POST") + "\n";
  
  server.send(404, "text/plain", message);
}

// ==================== CONFIGURATION INITIALE ====================

void setup() {
  // Initialisation du port série
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== Démarrage TTGO T-Display ===");
  
  // -------------------- CONNEXION WIFI --------------------
  Serial.println("Connexion au WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  // Attente de la connexion
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connecté!");
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ Échec connexion WiFi");
    Serial.println("Vérifiez vos identifiants dans config_wifi.h");
  }
  
  // -------------------- CONFIGURATION SERVEUR HTTP --------------------
  server.on("/", handleRoot);
  server.on("/temperature", handleTemperature);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("✓ Serveur HTTP démarré");
  Serial.println("\nEndpoints disponibles:");
  Serial.println("  - GET  /");
  Serial.println("  - GET  /temperature");
  
  // -------------------- INITIALISATION ÉCRAN TFT --------------------
  tft.init();
  tft.setRotation(1);  // Orientation paysage
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Affichage titre
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("IoT Project");
  
  tft.setTextSize(1);
  tft.setCursor(10, 35);
  tft.print("IP: ");
  tft.println(WiFi.localIP());
  
  Serial.println("\n✓ Écran TFT initialisé");
  Serial.println("=== Système prêt ===\n");
}

// ==================== BOUCLE PRINCIPALE ====================

void loop() {
  // Gestion des requêtes HTTP
  server.handleClient();
  
  // Lecture de la température
  float temp = readTemperature();
  
  // Affichage sur le moniteur série
  Serial.print("Température: ");
  Serial.print(temp, 1);
  Serial.println(" °C");
  
  // -------------------- AFFICHAGE SUR ÉCRAN TFT --------------------
  
  // Effacement de la zone de température (évite les traînées)
  tft.fillRect(0, 60, tft.width(), 80, TFT_BLACK);
  
  // Affichage de la température
  tft.setTextSize(3);
  tft.setCursor(10, 70);
  tft.print("Temp: ");
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.print(temp, 1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print(" C");
  
  // Barre visuelle de température (0-50°C)
  int barWidth = map(constrain(temp, 0, 50), 0, 50, 0, tft.width() - 20);
  tft.fillRect(10, 115, tft.width() - 20, 15, TFT_BLACK);
  
  // Couleur en fonction de la température
  uint16_t barColor = TFT_GREEN;
  if (temp > 30) barColor = TFT_ORANGE;
  if (temp > 35) barColor = TFT_RED;
  
  tft.fillRect(10, 115, barWidth, 15, barColor);
  tft.drawRect(10, 115, tft.width() - 20, 15, TFT_WHITE);
  
  // Attente avant prochaine lecture
  delay(1000);
}
