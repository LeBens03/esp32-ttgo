#include <WiFi.h>
#include <WebServer.h>
#include <math.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>

#include "config.h" 

// ==================== CONFIGURATION MATÉRIELLE ====================

// Pin de la thermistance (ADC)
const int thermistorPin = 36;

// Paramètres de la thermistance NTC
const float nominalResistance = 10000.0;
const float nominalTemp = 25.0;
const float beta = 3950.0;
const float seriesResistor = 10000.0;

// Pins pour le ventilateur et LED RGB
const int fanPWMPin = 25;
const int ledRedPin = 13;
const int ledGreenPin = 15;
const int ledBluePin = 2;

// PWM Configuration
const int pwmFreq = 5000;
const int pwmResolution = 8;

const bool DEBUG = true;

// Temp override (test)
bool tempOverrideEnabled = false;
float tempOverrideValue = 22.0;

// ==================== OBJETS GLOBAUX ====================

WebServer server(80);
TFT_eSPI tft = TFT_eSPI();

// ==================== ÉTAT DU SYSTÈME ====================

struct VentilatorState {
    String mode;              // "auto" or "manual"
    int fanSpeed;             // 0-100 (PWM duty cycle approx)
    int rgbRed;               // 0-255
    int rgbGreen;             // 0-255
    int rgbBlue;              // 0-255
    float temperature;        // Température actuelle

    // Les 3 seuils de température
    float thresholdSlow;      // Seuil activation SLOW
    float thresholdMedium;    // Seuil activation MEDIUM
    float thresholdFast;      // Seuil activation FAST
};

// Initialisation avec des valeurs par défaut cohérentes
VentilatorState state = {
        "auto",    // mode
        0,         // fanSpeed
        0, 0, 0,   // RGB (Off)
        22.0,      // temperature
        22.0,      // thresholdSlow
        26.0,      // thresholdMedium
        30.0       // thresholdFast
};

// ==================== SECURITY FUNCTIONS ====================

bool validateAuth() {
    if (!AUTH_ENABLED) {
        if (DEBUG) Serial.println("AUTH DISABLED - Request allowed");
        return true;
    }

    if (!server.hasHeader("Authorization")) {
        if (DEBUG) Serial.println("AUTH FAILED: No Authorization header");
        server.send(401, "application/json", "{\"error\":\"Unauthorized\",\"message\":\"Missing Authorization header\"}");
        return false;
    }

    String authHeader = server.header("Authorization");
    String expectedAuth = "Bearer " + String(AUTH_TOKEN);

    if (authHeader != expectedAuth) {
        if (DEBUG) {
            Serial.println("AUTH FAILED: Invalid token");
            Serial.println("   Expected: " + expectedAuth);
            Serial.println("   Received: " + authHeader);
        }
        server.send(401, "application/json", "{\"error\":\"Unauthorized\",\"message\":\"Invalid authentication token\"}");
        return false;
    }

    if (DEBUG) Serial.println("AUTH SUCCESS");
    return true;
}

// ==================== FONCTIONS UTILITAIRES ====================

float readTemperature() {
    int sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += analogRead(thermistorPin);
        delay(2);
    }
    int analogValue = sum / 5;
    float voltage = analogValue * 3.3 / 4095.0;
    if (voltage == 0) return -273.15;

    float resistance = (3.3 - voltage) / voltage * seriesResistor;
    float temperatureK = 1.0 / (1.0 / (nominalTemp + 273.15) + (1.0 / beta) * log(resistance / nominalResistance));
    return temperatureK - 273.15;
}

void applyFanSettings() {
    int pwmValue = map(state.fanSpeed, 0, 100, 0, 255);
    ledcWrite(fanPWMPin, pwmValue);

    if (state.fanSpeed == 0) {
        ledcWrite(ledRedPin, 0);
        ledcWrite(ledGreenPin, 0);
        ledcWrite(ledBluePin, 0);
        return;
    }

    ledcWrite(ledRedPin, state.rgbRed);
    ledcWrite(ledGreenPin, state.rgbGreen);
    ledcWrite(ledBluePin, state.rgbBlue);
}

void enforceColorLogic() {
    if (state.fanSpeed == 0) {
        state.rgbRed = 0; state.rgbGreen = 0; state.rgbBlue = 0; return;
    } else if (state.fanSpeed <= 30) {
        state.rgbRed = 255; state.rgbGreen = 0; state.rgbBlue = 0;
    } else if (state.fanSpeed <= 60) {
        state.rgbRed = 0; state.rgbGreen = 0; state.rgbBlue = 255;
    } else {
        state.rgbRed = 0; state.rgbGreen = 255; state.rgbBlue = 0;
    }
}

String speedToString(int speed) {
    if (speed == 0) return "";
    if (speed <= 30) return "slow";
    if (speed <= 60) return "medium";
    return "fast";
}

int stringToSpeed(String speedStr) {
    speedStr.toLowerCase();
    if (speedStr == "slow") return 30;
    if (speedStr == "medium") return 60;
    if (speedStr == "fast") return 100;
    return 0;
}

void updateAutoMode() {
    float t = state.temperature;

    if (t < state.thresholdSlow) state.fanSpeed = 0;
    else if (t < state.thresholdMedium) state.fanSpeed = 30;
    else if (t < state.thresholdFast) state.fanSpeed = 60;
    else state.fanSpeed = 100;

    enforceColorLogic();
    applyFanSettings();
}

void updateDisplay() {
    static int lastSpeed = -1;
    static String lastMode = "";
    static float lastTemp = -999;

    bool forceRefresh = (state.mode != lastMode);

    if (forceRefresh) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setCursor(20, 10);
        tft.println("SMART HOME IoT");
        lastSpeed = -1;
    }

    if (abs(state.temperature - lastTemp) > 0.1 || forceRefresh) {
        tft.fillRect(0, 40, tft.width(), 30, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(10, 45);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.print(state.temperature, 1);
        tft.print(" C");
        lastTemp = state.temperature;
    }

    if (state.mode != lastMode || forceRefresh) {
        tft.fillRect(120, 45, 100, 30, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(120, 45);
        if (state.mode == "auto") tft.setTextColor(TFT_GREEN, TFT_BLACK);
        else tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
        tft.print(state.mode == "auto" ? "AUTO" : "MAN");
        lastMode = state.mode;
    }

    if (state.fanSpeed != lastSpeed || forceRefresh) {
        tft.fillRect(0, 90, tft.width(), 40, TFT_BLACK);

        int barWidth = map(state.fanSpeed, 0, 100, 0, tft.width() - 20);
        tft.drawRect(10, 90, tft.width() - 20, 20, TFT_WHITE);

        uint16_t color = tft.color565(state.rgbRed, state.rgbGreen, state.rgbBlue);
        if (barWidth > 0) tft.fillRect(12, 92, barWidth - 4, 16, color);

        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(10, 115);
        tft.print("Vitesse: ");
        if(state.fanSpeed == 0) tft.print("OFF");
        else if(state.fanSpeed <= 30) tft.print("SLOW");
        else if(state.fanSpeed <= 60) tft.print("MEDIUM");
        else tft.print("FAST");

        lastSpeed = state.fanSpeed;
    }
}

// ==================== HANDLERS API (WITH AUTHENTICATION) ====================
void handleGetMode() {
    if (!validateAuth()) return;

    StaticJsonDocument<64> doc;
    doc["mode"] = state.mode;
    String res; serializeJson(doc, res);
    server.send(200, "application/json", res);
}

void handleSetMode() {
    if (!validateAuth()) return;

    if (!server.hasArg("plain")) { server.send(400, "application/json", "{}"); return; }
    StaticJsonDocument<64> doc;
    deserializeJson(doc, server.arg("plain"));

    String newMode = doc["mode"].as<String>();
    if (newMode == "auto" || newMode == "manual") {
        state.mode = newMode;
        if (state.mode == "auto") updateAutoMode(); 
        String res;
        doc["mode"] = state.mode;
        serializeJson(doc, res);
        server.send(200, "application/json", res);
    } else {
        server.send(400, "application/json", "{\"error\":\"Invalid mode\"}");
    }
}

// Retourne l'état complet, y compris les seuils pour le Frontend
void handleGetFanStatus() {
    if (!validateAuth()) return; 

    StaticJsonDocument<512> doc;
    doc["mode"] = state.mode;
    doc["speed"] = speedToString(state.fanSpeed);  // STRING
    doc["color"] = String(state.rgbRed) + "," + String(state.rgbGreen) + "," + String(state.rgbBlue);
    doc["temperature"] = round(state.temperature * 10) / 10.0;

    // Ajout des seuils pour que le Frontend puisse s'initialiser
    JsonObject thresholds = doc.createNestedObject("thresholds");
    thresholds["slow"] = state.thresholdSlow;
    thresholds["medium"] = state.thresholdMedium;
    thresholds["fast"] = state.thresholdFast;

    String res; serializeJson(doc, res);
    server.send(200, "application/json", res);
}

void handleSetManualFan() {
    if (!validateAuth()) return; 

    if (state.mode != "manual") {
        server.send(403, "application/json", "{\"error\":\"Switch to manual mode first\"}");
        return;
    }
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (!doc.containsKey("speed")) {
        server.send(400, "application/json", "{\"error\":\"Missing speed\"}");
        return;
    }

    // Accept BOTH string and integer for compatibility
    int speed = 0;

    if (doc["speed"].is<const char*>() || doc["speed"].is<String>()) {
        String speedStr = doc["speed"].as<String>(); // "slow", "medium", "fast"
        speed = stringToSpeed(speedStr);
    } else if (doc["speed"].is<int>()) {
        speed = doc["speed"].as<int>();
        if (speed < 0) speed = 0;
        if (speed > 100) speed = 100;
    } else {
        server.send(400, "application/json", "{\"error\":\"Invalid speed format\"}");
        return;
    }

    state.fanSpeed = speed;

    // Force la couleur selon la spécification
    enforceColorLogic();
    applyFanSettings();

    // Réponse avec speed au format STRING
    StaticJsonDocument<256> resDoc;
    resDoc["speed"] = speedToString(state.fanSpeed);
    resDoc["color"] = String(state.rgbRed) + "," + String(state.rgbGreen) + "," + String(state.rgbBlue);

    String res; serializeJson(resDoc, res);
    server.send(200, "application/json", res);

    if (DEBUG) Serial.println("Manual Set: speed=" + String(state.fanSpeed) + " (" + speedToString(state.fanSpeed) + ")");
}

// Configuration des 3 seuils
void handleSetThresholds() {
    if (!validateAuth()) return;

    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"Missing body\"}"); return; }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }

    if (doc.containsKey("slow") && doc.containsKey("medium") && doc.containsKey("fast")) {
        float s = doc["slow"];
        float m = doc["medium"];
        float f = doc["fast"];

        // Validation logique : Slow < Medium < Fast
        if (s < m && m < f && s >= 0 && f <= 50) {
            state.thresholdSlow = s;
            state.thresholdMedium = m;
            state.thresholdFast = f;

            if (state.mode == "auto") updateAutoMode();

            // Réponse
            StaticJsonDocument<128> resDoc;
            resDoc["success"] = true;
            String res; serializeJson(resDoc, res);
            server.send(200, "application/json", res);

            if (DEBUG) Serial.printf("New Thresholds: %.1f / %.1f / %.1f\n", s, m, f);
        } else {
            server.send(400, "application/json", "{\"error\":\"Invalid thresholds (must be slow < medium < fast and 0-50)\"}");
        }
    } else {
        server.send(400, "application/json", "{\"error\":\"Missing slow/medium/fast keys\"}");
    }
}

void handleGetTemperature() {
    if (!validateAuth()) return;

    StaticJsonDocument<64> doc;
    doc["temperature"] = round(state.temperature * 10) / 10.0;
    String res; serializeJson(doc, res);
    server.send(200, "application/json", res);
}

void handleSetTemperature() {
    if (!validateAuth()) return; 

    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing request body\"}");
        return;
    }

    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    // Revenir au mode normal
    if (doc.containsKey("mode")) {
        String mode = doc["mode"].as<String>();
        mode.toLowerCase();

        if (mode == "sensor" || mode == "normal") {
            tempOverrideEnabled = false;

            StaticJsonDocument<128> res;
            res["override"] = false;
            res["mode"] = "sensor";
            String out; serializeJson(res, out);
            server.send(200, "application/json", out);

            if (DEBUG) Serial.println("PUT /temperature - Override disabled (sensor mode)");
            return;
        }

        server.send(400, "application/json", "{\"error\":\"Invalid mode. Use 'sensor'\"}");
        return;
    }

    // Forcer une température
    if (!doc.containsKey("value")) {
        server.send(400, "application/json", "{\"error\":\"Missing value\"}");
        return;
    }

    float value = doc["value"];
    if (value < -20 || value > 60) {
        server.send(400, "application/json", "{\"error\":\"Temperature must be between -20 and 60\"}");
        return;
    }

    tempOverrideEnabled = true;
    tempOverrideValue = value;

    state.temperature = value;
    if (state.mode == "auto") updateAutoMode();

    StaticJsonDocument<128> res;
    res["override"] = true;
    res["temperature"] = value;
    String out; serializeJson(res, out);
    server.send(200, "application/json", out);

    if (DEBUG) Serial.println("PUT /temperature - Override enabled: " + String(value, 1) + "C");
}

void handleNotFound() {
    server.send(404, "application/json", "{\"error\":\"Not found\"}");
}

// ==================== SETUP ====================

void setup() {
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Booting...");

    ledcAttach(fanPWMPin, pwmFreq, pwmResolution);
    ledcAttach(ledRedPin, pwmFreq, pwmResolution);
    ledcAttach(ledGreenPin, pwmFreq, pwmResolution);
    ledcAttach(ledBluePin, pwmFreq, pwmResolution);

    applyFanSettings();

    // WiFi depuis config.h
    Serial.print("Connecting to "); Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected.");
    Serial.println(WiFi.localIP());

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(10, 10);
    tft.setTextSize(1);
    tft.println("IP: " + WiFi.localIP().toString());

    printQRCodeData();

    // Routes API
    server.on("/mode", HTTP_GET, handleGetMode);
    server.on("/mode", HTTP_PUT, handleSetMode);
    server.on("/fan/status", HTTP_GET, handleGetFanStatus);
    server.on("/fan/manual", HTTP_PUT, handleSetManualFan);
    server.on("/fan/threshold", HTTP_PUT, handleSetThresholds);
    server.on("/temperature", HTTP_GET, handleGetTemperature);
    server.on("/temperature", HTTP_PUT, handleSetTemperature);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("HTTP Server started");

    if (AUTH_ENABLED) Serial.println("Security: ENABLED - All requests require authentication");
    else Serial.println("Security: DISABLED - All requests are allowed (TESTING MODE)");
}

// ==================== LOOP ====================

void loop() {
    server.handleClient();

    static unsigned long lastTempRead = 0;
    if (millis() - lastTempRead >= 1000) {
        lastTempRead = millis();

        if (tempOverrideEnabled) {
            state.temperature = tempOverrideValue;
        } else {
            float rawTemp = readTemperature();
            state.temperature = (state.temperature * 0.7) + (rawTemp * 0.3);
        }

        if (state.mode == "auto") updateAutoMode();

        updateDisplay();
    }
}
