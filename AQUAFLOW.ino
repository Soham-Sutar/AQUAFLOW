#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// -----------------------------
// WiFi and Firebase credentials
// -----------------------------
#define WIFI_SSID "your wifi name"
#define WIFI_PASSWORD "your wifi password"
#define API_KEY "API key"
#define DATABASE_URL "URL"
#define DATABASE_SECRET "secret key"

// -----------------------------
// Firebase objects
// -----------------------------
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

#define IN1 26
#define IN2 25
#define IN3 27
#define IN4 33
#define ENA 14
#define ENB 12
#define SPEED1 180
#define SPEED2 130

#define SOIL_PIN 32

int p1state = 0, p2state = 0;

// Function declarations
void pumpOn(uint8_t pump_no);
void pumpOff(uint8_t pump_no);
void toggle(uint8_t pump_no);
void soilMoisture();
void pumpAuto_nonblocking();
void timeOut(String command);

// --- Timing and intervals ---
unsigned long previousMillis = 0;
// comment said 10 seconds — set to 10000 ms
const long soilInterval = 300L; // 0.3 seconds

// Command poll interval (don't poll database too fast)
unsigned long lastCommandPoll = 0;
const unsigned long commandPollInterval = 500L; // 500 ms (tune as needed)

// Timeout logic
unsigned long lastCommandTime = 0;
bool pumpOffTriggered = false;
const unsigned long timeoutDuration = 120000UL;  // 120 seconds

// pumpAuto state machine variables
int autoState = 0; // 0 = idle, 1 = running
int autoPhase = 0; // phase inside auto routine
unsigned long autoPhaseStart = 0;

// last sent soil value to avoid frequent writes
int lastSoilPercent = -1;

void setup() {
  Serial.begin(115200);

  // --- Wi-Fi connection ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false); // disable WiFi sleep to improve responsiveness

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
  Serial.println("\n✅ Connected to Wi-Fi");

  // --- Firebase setup ---
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("✅ Connected to Firebase");

  // initialize DB statuses
  Firebase.RTDB.setInt(&fbdo, "/ESP_32/pump1_status", p1state);
  Firebase.RTDB.setInt(&fbdo, "/ESP_32/pump2_status", p2state);

  // --- Motor setup ---
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENB, OUTPUT);

  pumpOff(3); // turn off both at start

  previousMillis = millis();
  lastCommandPoll = millis();
  lastCommandTime = millis();
}

// --- Timeout function ---
void timeOut(String command) {
  if (command == "standby") {
    if (millis() - lastCommandTime > timeoutDuration && !pumpOffTriggered) {
      pumpOff(3);
      Serial.println("⚠ Timeout reached: Pumps turned off automatically.");
      pumpOffTriggered = true;  // prevent repeated trigger
    }
    return; // stop further execution if standby
  } else {
    // Reset on receiving any new (non-standby) command
    lastCommandTime = millis();
    pumpOffTriggered = false;
  }
}

// -----------------------------
// Main loop
// -----------------------------
void loop() {
  unsigned long now = millis();

  // soil reading (non-blocking interval)
  if (now - previousMillis >= soilInterval) {
    previousMillis = now;
    soilMoisture();
  }

  // run non-blocking pumpAuto state machine (if active)
  if (autoState == 1) pumpAuto_nonblocking();

  // Poll command at a throttled rate (avoid repeated network calls)
  if (now - lastCommandPoll >= commandPollInterval) {
    lastCommandPoll = now;

    if (Firebase.RTDB.getString(&fbdo, "/ESP_32/command") && fbdo.dataType() == "string") {
      String command = fbdo.stringData();
      command.replace("\"", "");
      command.replace("\\", "");
      command.trim();

      // handle timeout logic first
      timeOut(command);

      // If standby, skip processing further by using an if/else structure
      if (command == "standby") {
        // do nothing this poll
      } else {
        Serial.print("🔥 Command: ");
        Serial.println(command);

        bool executed = false;

        if (command == "pump1_toggle") {
          toggle(1);
          executed = true;
        } else if (command == "pump2_toggle") {
          toggle(2);
          executed = true;
        } else if (command == "both_on") {
          pumpOn(3);
          executed = true;
        } else if (command == "both_off") {
          pumpOff(3);
          executed = true;
        } else if (command == "auto") {
          // start non-blocking auto routine
          autoState = 1;
          autoPhase = 0;
          autoPhaseStart = millis();
          Firebase.RTDB.setInt(&fbdo, "/ESP_32/autoState", 1);
          executed = true;
        }

        if (executed) {
          // set command back to standby so same command won't re-run
          Firebase.RTDB.setString(&fbdo, "/ESP_32/command", "standby");
        }
      }
    } else {
      // optional: handle getString failure or absence of command
      // Serial.println("No command or getString failed");
    }
  }

  // brief yield so background tasks can run
  delay(1);
}

// -----------------------------
// Motor control functions
// -----------------------------
void pumpOn(uint8_t pump_no) {
  Serial.printf("🚿 Pump %d ON\n", pump_no);
  bool changed = false;
  if (pump_no == 1 || pump_no == 3) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, SPEED1);
    if (p1state != 1) changed = true;
    p1state = 1;
  }
  if (pump_no == 2 || pump_no == 3) {
    digitalWrite(IN3, HIGH);
    digitalWrite(IN4, LOW);
    analogWrite(ENB, SPEED2);
    if (p2state != 1) changed = true;
    p2state = 1;
  }
  // Only update Firebase if something changed
  if (changed) {
    Firebase.RTDB.setInt(&fbdo, "/ESP_32/pump1_status", p1state);
    Firebase.RTDB.setInt(&fbdo, "/ESP_32/pump2_status", p2state);
  }
}

void pumpOff(uint8_t pump_no) {
  Serial.printf("🛑 Pump %d OFF\n", pump_no);
  bool changed = false;
  if (pump_no == 1 || pump_no == 3) {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    analogWrite(ENA, 0);
    if (p1state != 0) changed = true;
    p1state = 0;
  }
  if (pump_no == 2 || pump_no == 3) {
    digitalWrite(IN3, LOW);
    digitalWrite(IN4, LOW);
    analogWrite(ENB, 0);
    if (p2state != 0) changed = true;
    p2state = 0;
  }
  if (changed) {
    Firebase.RTDB.setInt(&fbdo, "/ESP_32/pump1_status", p1state);
    Firebase.RTDB.setInt(&fbdo, "/ESP_32/pump2_status", p2state);
  }
}

void toggle(uint8_t pump_no) {
  if (pump_no == 1) {
    (p1state) ? pumpOff(1) : pumpOn(1);
  } else if (pump_no == 2) {
    (p2state) ? pumpOff(2) : pumpOn(2);
  }
}

// -----------------------------
// Non-blocking pumpAuto
// -----------------------------
void pumpAuto_nonblocking() {
  unsigned long now = millis();
  switch (autoPhase) {
    case 0:
      // Start: turn both pumps ON
      pumpOn(3);
      autoPhaseStart = now;
      autoPhase = 1;
      Serial.println("Auto: started, both ON");
      break;
    case 1:
      // Wait 60 seconds
      if (now - autoPhaseStart >= 60000UL) {
        pumpOff(2); // turn off pump2
        autoPhaseStart = now;
        autoPhase = 2;
        Serial.println("Auto: pump2 OFF, waiting for pump1 off phase");
      }
      break;
    case 2:
      // Wait 30 seconds then stop pump1 and finish
      if (now - autoPhaseStart >= 30000UL) {
        pumpOff(1);
        autoPhase = 0;
        autoState = 0; // auto routine finished
        Firebase.RTDB.setInt(&fbdo, "/ESP_32/autoState", 0);
        Serial.println("Auto: finished, both OFF");
      }
      break;
    default:
      autoPhase = 0;
      autoState = 0;
      break;
  }
}

// -----------------------------
// Soil moisture (reduced writes)
// -----------------------------
void soilMoisture() {
  int sensorValue = analogRead(SOIL_PIN);
  int moisturePercent;

  if (sensorValue > 2000) {
    moisturePercent = map(sensorValue, 4095, 2000, 0, 25);
  } else if (sensorValue > 1300) {
    moisturePercent = map(sensorValue, 2000, 1300, 25, 60);
  } else if (sensorValue > 900) {
    moisturePercent = map(sensorValue, 1300, 900, 60, 90);
  } else {
    moisturePercent = map(sensorValue, 900, 800, 90, 100);
  }

  if (moisturePercent < 0) moisturePercent = 0;
  if (moisturePercent > 100) moisturePercent = 100;

  const int changeThreshold = 2;
  if (lastSoilPercent < 0 || abs(moisturePercent - lastSoilPercent) >= changeThreshold) {
    if (Firebase.RTDB.setInt(&fbdo, "/ESP_32/response", moisturePercent)) {
      lastSoilPercent = moisturePercent;
    } else {
      Serial.println("Soil write failed: " + fbdo.errorReason());
    }
  }
}
