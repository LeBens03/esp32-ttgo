#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
// Configuration WiFi (fichier séparé non versionné)
#include "config_wifi.h"

// ==================== CONFIGURATION MATÉRIELLE ====================

// Pin de la thermistance (ADC)
const int thermistorPin = 36;

// Paramètres de la thermistance NTC
const float nominalResistance = 10000.0;
const float nominalTemp = 25.0;
const float beta = 3950.0;
const float seriesResistor = 10000.0;

// Pins pour le ventilateur et LED RGB
const int fanPWMPin = 25;        // Pin PWM pour contrôle vitesse ventilateur
const int ledRedPin = 13;        // Pin LED Rouge
const int ledGreenPin = 15;      // Pin LED Verte
const int ledBluePin = 2;        // Pin LED Bleue

// PWM Configuration
const int pwmFreq = 5000;        // Fréquence PWM 5kHz (compatible DC fans)
const int pwmResolution = 8;     // 8-bit (0-255)

// Debug flag
const bool DEBUG = true;         // Set to false to disable serial logs

// ==================== OBJETS GLOBAUX ====================

WebServer server(80);
TFT_eSPI tft = TFT_eSPI();

// ==================== ÉTAT DU SYSTÈME (MODEL) ====================

struct VentilatorState {
    String mode;              // "auto" or "manual"
    int fanSpeed;             // 0-100
    int rgbRed;               // 0-255
    int rgbGreen;             // 0-255
    int rgbBlue;              // 0-255
    float temperature;        // Current temperature
    float autoThreshold;      // Threshold for auto mode
};

VentilatorState state = {
        "auto",    // mode
        0,         // fanSpeed
        0,         // rgbRed
        255,       // rgbGreen
        0,         // rgbBlue
        22.0,      // temperature
        25.0       // autoThreshold
};

// ==================== FONCTIONS UTILITAIRES ====================

/**
 * Lecture de la température via thermistance NTC
 */
float readTemperature() {
    // Lecture multiple pour moyenner le bruit
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += analogRead(thermistorPin);
        delay(2);
    }
    int analogValue = sum / 5;

    float voltage = analogValue * 3.3 / 4095.0;

    // Formule pour: 3.3V -> NTC -> ADC -> R_series -> GND
    float resistance = (3.3 - voltage) / voltage * seriesResistor;

    // Équation β de Steinhart-Hart
    float temperatureK = 1.0 / (
            1.0 / (nominalTemp + 273.15) +
            (1.0 / beta) * log(resistance / nominalResistance)
    );

    return temperatureK - 273.15;
}

/**
 * Parse RGB string "R,G,B" et extrait les valeurs
 */
bool parseRGB(String colorStr, int &r, int &g, int &b) {
    int firstComma = colorStr.indexOf(',');
    int secondComma = colorStr.indexOf(',', firstComma + 1);

    if (firstComma == -1 || secondComma == -1) return false;

    r = colorStr.substring(0, firstComma).toInt();
    g = colorStr.substring(firstComma + 1, secondComma).toInt();
    b = colorStr.substring(secondComma + 1).toInt();

    return (r >= 0 && r <= 255 && g >= 0 && g <= 255 && b >= 0 && b <= 255);
}

/**
 * Applique les réglages du ventilateur et des LEDs
 * Compatible avec ESP32 Arduino Core 2.x et 3.x
 */
void applyFanSettings() {
    // Conversion vitesse 0-100 vers PWM 0-255
    int pwmValue = map(state.fanSpeed, 0, 100, 0, 255);
    ledcWrite(fanPWMPin, pwmValue);

    // Application des couleurs RGB via PWM
    ledcWrite(ledRedPin, state.rgbRed);
    ledcWrite(ledGreenPin, state.rgbGreen);
    ledcWrite(ledBluePin, state.rgbBlue);
}

/**
 * Logique du mode automatique
 */
void updateAutoMode() {
    float diff = state.temperature - state.autoThreshold;

    if (diff <= 0) {
        // Below threshold: fan off, green
        state.fanSpeed = 0;
        state.rgbRed = 0; state.rgbGreen = 255; state.rgbBlue = 0;
    } else if (diff <= 3) {
        // 0-3°C above: low speed, yellow
        state.fanSpeed = 30;
        state.rgbRed = 255; state.rgbGreen = 255; state.rgbBlue = 0;
    } else if (diff <= 6) {
        // 3-6°C above: medium speed, orange
        state.fanSpeed = 60;
        state.rgbRed = 255; state.rgbGreen = 165; state.rgbBlue = 0;
    } else {
        // >6°C above: high speed, red
        state.fanSpeed = 100;
        state.rgbRed = 255; state.rgbGreen = 0; state.rgbBlue = 0;
    }

    applyFanSettings();
}

/**
 * Mise à jour de l'affichage TFT
 */
/*
void updateDisplay() {
   static float lastTemp = -999;

   // Mise à jour uniquement si la température change
   if (abs(state.temperature - lastTemp) > 0.1) {

       // Effacer tout l'écran
       tft.fillScreen(TFT_BLACK);

       // Titre
       tft.setTextSize(2);
       tft.setTextColor(TFT_CYAN, TFT_BLACK);
       tft.setCursor(20, 10);
       tft.println("TEMPERATURE");

       // Affichage température seule
       tft.setTextSize(3);
       tft.setCursor(20, 60);
       tft.setTextColor(TFT_YELLOW, TFT_BLACK);
       tft.print(state.temperature, 1);
       tft.println(" C");

       lastTemp = state.temperature;
   }
}

*/
void updateDisplay() {
    static int lastSpeed = -1;
    static String lastMode = "";
    static float lastTemp = -999;
    static bool firstDisplay = true;

    // Affichage initial complet
    if (firstDisplay) {
        tft.fillScreen(TFT_BLACK);

        // Titre centré
        tft.setTextSize(2);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(20, 10);
        tft.println("VENTILATEUR IoT");

        firstDisplay = false;

        // Forcer l'affichage initial
        lastMode = "";
        lastTemp = -999;
        lastSpeed = -1;
    }

    // AFFICHAGE TEMPÉRATURE ET MODE SUR UNE SEULE LIGNE
    if (state.mode != lastMode || abs(state.temperature - lastTemp) > 0.1) {
        // Effacer la zone d'affichage
        tft.fillRect(0, 50, tft.width(), 25, TFT_BLACK);

        // Construire la chaîne complète
        String displayLine = "Temperature: " + String(state.temperature, 1) + "C | Mode: ";

        // Afficher la première partie (température)
        tft.setTextSize(1);
        tft.setCursor(5, 55);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.print("Temperature: ");
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.print(state.temperature, 1);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.print("C | Mode: ");

        // Afficher le mode avec couleur appropriée
        if (state.mode == "auto") {
            tft.setTextColor(TFT_CYAN, TFT_BLACK);
            tft.print("AUTO");
        } else {
            tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
            tft.print("MANUAL");
        }

        lastMode = state.mode;
        lastTemp = state.temperature;
    }

    // AFFICHAGE DE LA VITESSE
    if (state.fanSpeed != lastSpeed) {
        tft.fillRect(0, 90, tft.width(), 35, TFT_BLACK);

        tft.setTextSize(1);
        tft.setCursor(10, 95);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.print("Ventilateur: ");
        tft.print(state.fanSpeed);
        tft.print("%");
        lastSpeed = state.fanSpeed;

        // Barre de progression
        int barWidth = map(state.fanSpeed, 0, 100, 0, tft.width() - 20);
        tft.fillRect(10, 110, tft.width() - 20, 12, TFT_BLACK);

        uint16_t displayColor = tft.color565(state.rgbRed, state.rgbGreen, state.rgbBlue);
        if (barWidth > 0) {
            tft.fillRect(10, 110, barWidth, 12, displayColor);
        }
        tft.drawRect(10, 110, tft.width() - 20, 12, TFT_WHITE);
    }
}
// ==================== GESTIONNAIRES HTTP (HANDLERS) ====================

void handleGetMode() {
    StaticJsonDocument<256> doc;
    doc["mode"] = state.mode;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    if (DEBUG) Serial.println("GET /mode - Mode: " + state.mode);
}

void handleSetMode() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing request body\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    String newMode = doc["mode"].as<String>();

    if (newMode != "auto" && newMode != "manual") {
        server.send(400, "application/json", "{\"error\":\"Invalid mode. Must be auto or manual\"}");
        return;
    }

    state.mode = newMode;

    if (newMode == "auto") {
        updateAutoMode();
    }

    StaticJsonDocument<256> response;
    response["mode"] = state.mode;
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);

    if (DEBUG) Serial.println("PUT /mode - New mode: " + state.mode);
}

void handleGetFanStatus() {
    StaticJsonDocument<512> doc;
    doc["mode"] = state.mode;
    doc["speed"] = state.fanSpeed;
    doc["color"] = String(state.rgbRed) + "," + String(state.rgbGreen) + "," + String(state.rgbBlue);
    doc["temperature"] = round(state.temperature * 10) / 10.0;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    if (DEBUG) Serial.println("GET /fan/status");
}

void handleSetManualFan() {
    if (state.mode != "manual") {
        server.send(403, "application/json", "{\"error\":\"Cannot set manual controls while in auto mode\"}");
        return;
    }

    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing request body\"}");
        return;
    }

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.containsKey("speed") || !doc.containsKey("color")) {
        server.send(400, "application/json", "{\"error\":\"Missing speed or color\"}");
        return;
    }

    int speed = doc["speed"];
    String color = doc["color"].as<String>();

    if (speed < 0 || speed > 100) {
        server.send(400, "application/json", "{\"error\":\"Speed must be between 0 and 100\"}");
        return;
    }

    int r, g, b;
    if (!parseRGB(color, r, g, b)) {
        server.send(400, "application/json", "{\"error\":\"Color must be in R,G,B format with values 0-255\"}");
        return;
    }

    state.fanSpeed = speed;
    state.rgbRed = r;
    state.rgbGreen = g;
    state.rgbBlue = b;

    applyFanSettings();

    StaticJsonDocument<512> response;
    response["speed"] = state.fanSpeed;
    response["color"] = color;
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);

    if (DEBUG) Serial.println("PUT /fan/manual - Speed: " + String(speed) + ", Color: " + color);
}

void handleSetThreshold() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing request body\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.containsKey("threshold")) {
        server.send(400, "application/json", "{\"error\":\"Missing threshold\"}");
        return;
    }

    float threshold = doc["threshold"];

    if (threshold < 0 || threshold > 50) {
        server.send(400, "application/json", "{\"error\":\"Threshold must be between 0 and 50\"}");
        return;
    }

    state.autoThreshold = threshold;

    if (state.mode == "auto") {
        updateAutoMode();
    }

    StaticJsonDocument<256> response;
    response["threshold"] = state.autoThreshold;
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);

    if (DEBUG) Serial.println("PUT /fan/threshold - New threshold: " + String(threshold));
}

void handleGetTemperature() {
    StaticJsonDocument<256> doc;
    doc["temperature"] = round(state.temperature * 10) / 10.0;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);

    if (DEBUG) Serial.println("GET /temperature - Temp: " + String(state.temperature, 1) + "°C");
}

void handleSetTemperature() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing request body\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));

    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.containsKey("value")) {
        server.send(400, "application/json", "{\"error\":\"Missing value\"}");
        return;
    }

    float value = doc["value"];

    if (value < -20 || value > 60) {
        server.send(400, "application/json", "{\"error\":\"Temperature must be between -20 and 60\"}");
        return;
    }

    state.temperature = value;

    if (state.mode == "auto") {
        updateAutoMode();
    }

    StaticJsonDocument<256> response;
    response["temperature"] = state.temperature;
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);

    if (DEBUG) Serial.println("PUT /temperature - New temp: " + String(value, 1) + "°C");
}

void handleNotFound() {
    StaticJsonDocument<512> doc;
    doc["error"] = "Route not found";
    doc["uri"] = server.uri();
    doc["method"] = (server.method() == HTTP_GET) ? "GET" : "PUT";

    String response;
    serializeJson(doc, response);
    server.send(404, "application/json", response);
}

// ==================== CONFIGURATION INITIALE ====================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== Démarrage IoT Ventilator System ===");

    // -------------------- INITIALISATION ÉCRAN TFT --------------------
    tft.init();
    tft.setRotation(1); // Mode paysage
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    // Affichage message de démarrage simple
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.println("VENTILATEUR IoT");

    tft.setTextSize(1);
    tft.setCursor(10, 35);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("Connexion WiFi...");

    Serial.println("✓ Écran TFT initialisé");

    // -------------------- CONFIGURATION PWM & GPIO --------------------
    ledcAttach(fanPWMPin, pwmFreq, pwmResolution);
    ledcAttach(ledRedPin, pwmFreq, pwmResolution);
    ledcAttach(ledGreenPin, pwmFreq, pwmResolution);
    ledcAttach(ledBluePin, pwmFreq, pwmResolution);

    applyFanSettings();
    Serial.println("✓ GPIO et PWM configurés");

    // -------------------- CONNEXION WIFI --------------------
    Serial.println("Connexion au WiFi...");
    Serial.print("SSID: ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");

        // Animation sur l'écran
        tft.fillRect(10 + (attempts % 20) * 5, 50, 5, 5, TFT_GREEN);

        attempts++;
    }

    // Effacement de la ligne de connexion
    tft.fillRect(0, 35, tft.width(), 30, TFT_BLACK);

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n✓ WiFi connecté!");
        Serial.println("╔════════════════════════════════════╗");
        Serial.print("║ Adresse IP: ");
        Serial.print(WiFi.localIP());
        Serial.println("          ║");
        Serial.println("╚════════════════════════════════════╝");

        // Message de succès sur l'écran (sans IP)
        tft.setCursor(10, 35);
        tft.setTextSize(1);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.println("WiFi: CONNECTE");
    } else {
        Serial.println("\n✗ Échec connexion WiFi");

        tft.setCursor(10, 35);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.println("WiFi: ERREUR");
    }

    // -------------------- CONFIGURATION SERVEUR HTTP --------------------
    server.on("/mode", HTTP_GET, handleGetMode);
    server.on("/mode", HTTP_PUT, handleSetMode);

    server.on("/fan/status", HTTP_GET, handleGetFanStatus);
    server.on("/fan/manual", HTTP_PUT, handleSetManualFan);
    server.on("/fan/threshold", HTTP_PUT, handleSetThreshold);

    server.on("/temperature", HTTP_GET, handleGetTemperature);
    server.on("/temperature", HTTP_PUT, handleSetTemperature);

    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("✓ Serveur HTTP démarré");
    Serial.println("\nEndpoints disponibles:");
    Serial.println("  - GET  /mode");
    Serial.println("  - PUT  /mode");
    Serial.println("  - GET  /fan/status");
    Serial.println("  - PUT  /fan/manual");
    Serial.println("  - PUT  /fan/threshold");
    Serial.println("  - GET  /temperature");
    Serial.println("  - PUT  /temperature");

    Serial.println("\n=== Système prêt ===\n");

    // Délai avant d'afficher l'interface principale
    delay(2000);

    // Forcer le premier affichage de l'interface
    updateDisplay();
    Serial.println("✓ Interface TFT affichée");
}

// ==================== BOUCLE PRINCIPALE ====================

void loop() {
    server.handleClient();

    // Lecture de la température toutes les secondes
    static unsigned long lastTempRead = 0;
    if (millis() - lastTempRead >= 1000) {
        lastTempRead = millis();

        // Lecture avec lissage exponentiel
        float rawTemp = readTemperature();
        state.temperature = (state.temperature * 0.7) + (rawTemp * 0.3);

        // Mise à jour auto si activé
        if (state.mode == "auto") {
            updateAutoMode();
        }

        // Mise à jour de l'affichage
        updateDisplay();
    }
}