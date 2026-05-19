# AQUAFLOW
# ESP32 — Firebase Pump Controller

This README explains the ESP32 sketch, the Firebase Realtime Database structure it expects, and how to build a companion Android app using **MIT App Inventor**. Use this document to set up the hardware, configure Firebase, deploy the ESP32 code, and create a simple mobile UI that sends commands and reads state from Firebase.

---

## Table of contents

1. Overview
2. Features
3. Hardware required
4. Wiring / Pin mapping
5. Libraries & software prerequisites
6. Firebase setup
7. ESP32 sketch: overview and important notes
8. MIT App Inventor: components & blocks (how-to)
9. Database structure (example JSON)
10. Troubleshooting & optimizations
11. Security & deployment notes
12. FAQ

---

## 1. Overview

This project uses an ESP32 to control two pumps (via an L298N-style motor driver). The ESP32 reads a soil moisture sensor and sends the moisture percentage to Firebase. A mobile app (MIT App Inventor) writes commands into the Firebase Realtime Database so the ESP32 can read and act on them.

Commands are simple strings (`pump1_toggle`, `pump2_toggle`, `both_on`, `both_off`, `auto`, `standby`). The ESP32 polls the RTDB frequently (every ~150 ms in this sketch) and acts when it sees a non-`standby` command.

---

## 2. Features

* Control two pumps independently or both together
* Manual toggles and an `auto` routine
* Soil-moisture reading uploaded to Firebase
* Pump status published to Firebase (`pump1_status`, `pump2_status`)
* Simple MIT App Inventor UI to send commands and display status

---

## 3. Hardware required

* ESP32 (WROOM) board
* L298N motor driver (or equivalent H-bridge) for the pumps
* 2 DC pumps (or motors) compatible with your driver and supply
* Soil moisture analog sensor
* Proper power supply for motors (e.g. 12V) and for the ESP32 (5V via USB or regulated 3.3V) — do **not** feed 12V directly to the ESP32
* Connecting wires, breadboard, resistors as needed

---

## 4. Wiring / Pin mapping

This is the pin mapping used by the sketch. Update pins in the code if your wiring differs.

| Function          | ESP32 Pin |
| ----------------- | --------- |
| Pump 1 IN1        | 26        |
| Pump 1 IN2        | 25        |
| Pump 1 ENA (PWM)  | 14        |
| Pump 2 IN3        | 27        |
| Pump 2 IN4        | 33        |
| Pump 2 ENB (PWM)  | 12        |
| Soil analog input | 32        |

Notes:

* ENA/ENB are PWM pins. Depending on your motor driver you might also need to connect driver enable jumpers.
* Pumps should be powered from the motor supply (e.g. 12V). Ensure common GND between motor supply and ESP32 ground.
* The soil sensor uses the ESP32 ADC (0..4095). Calibrate the mapping ranges to your sensor and soil.

---

## 5. Libraries & software prerequisites

* Arduino IDE or PlatformIO with ESP32 board support installed
* Libraries used in the sketch:

  * `Firebase_ESP_Client` (for RTDB interactions)
  * `WiFi.h` (ESP32 builtin)
* MIT App Inventor (web app) to create the Android companion app

Install `Firebase_ESP_Client` and its addons (`TokenHelper.h`, `RTDBHelper.h`) via Library Manager or from the library repository.

---

## 6. Firebase setup

1. Create a Firebase project at console.firebase.google.com.
2. In **Realtime Database** create a database and set rules. For testing you may use relaxed rules but for production lock it down and require authentication.

Example minimal rules for development (NOT for production):

```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

3. In *Project settings* -> *General* copy the `API Key` and the `Realtime Database URL` — these go into the sketch as `API_KEY` and `DATABASE_URL`.
4. If you are using the legacy database secret, you can find it under *Project Settings* -> *Service accounts* -> *Database secrets* (deprecated in new projects). Prefer using Firebase Authentication or proper tokens instead of embedding legacy secrets.

**RTDB paths used by the sketch**

* `/ESP_32/command` — string: command to execute
* `/ESP_32/pump1_status` — integer (0 or 1)
* `/ESP_32/pump2_status` — integer (0 or 1)
* `/ESP_32/autoState` — integer (0 or 1) showing `auto` routine running
* `/ESP_32/response` — integer: calculated soil moisture percentage

---

## 7. ESP32 sketch: overview and important notes

Key constants and logic:

* `checkInterval` = 150ms: ESP checks the `/ESP_32/command` node every 150ms.
* `interval` = 5000ms: soil moisture measurement is uploaded roughly every 5 seconds.
* Commands implemented: `pump1_toggle`, `pump2_toggle`, `both_on`, `both_off`, `auto`.
* After executing a command the ESP writes `standby` back into `/ESP_32/command`.

### Important implementation notes and suggestions

* The sketch updates the pump states to the DB every time a pump on/off occurs. This helps the mobile UI stay in sync.
* Currently the code calls `return;` when it reads `"standby"`. That returns from `loop()` immediately — which is usually fine because `loop()` restarts, but if you want additional checks or code after the command handling block, remove `return;` so the code continues executing in the same loop iteration.
* When writing `standby` back to the DB, be sure the `setString()` call succeeds — you might optionally check the return boolean before proceeding.

### `pumpAuto()` routine

It sets `autoState = 1`, turns both pumps on, waits for 10 seconds, turns off pump 1, waits 5 seconds, then turns off pump 2 and clears `autoState`.

This function uses `delay()` which blocks the main loop for the duration. If you need responsiveness while auto runs, convert it into a non-blocking state machine using `millis()`.

---

## 8. MIT App Inventor: components & blocks (how-to)

### Components (suggested)

* `FirebaseDB` component (set `FirebaseURL`, `FirebaseToken`/Auth token)
* Buttons: `btnPump1`, `btnPump2`, `btnBothOn`, `btnBothOff`, `btnAuto`
* Labels: `lblPump1Status`, `lblPump2Status`, `lblSoilMoisture`
* Notifier (optional) for error messages

### Basic behavior

1. When user taps a button, call `FirebaseDB.StoreValue` with tag `/ESP_32/command` and value one of the command strings:

   * Pump1 toggle: `pump1_toggle`
   * Pump2 toggle: `pump2_toggle`
   * Both on: `both_on`
   * Both off: `both_off`
   * Auto: `auto`

2. To display pump states and soil moisture, have the app either:

   * Periodically call `FirebaseDB.GetValue` for `/ESP_32/pump1_status`, `/ESP_32/pump2_status`, `/ESP_32/response` and update labels on `GotValue` event, OR
   * Use `FirebaseDB`'s tag change event (if available in your AI2 extension) so the app is notified when the value changes.

### Example MIT AI2 pseudoblocks

```
When btnPump1.Click
  FirebaseDB.StoreValue tag: "/ESP_32/command" value: "pump1_toggle"

When FirebaseDB.GotValue tag = "/ESP_32/pump1_status"
  set lblPump1Status.Text to value

When FirebaseDB.GotValue tag = "/ESP_32/response"
  set lblSoilMoisture.Text to value + "%"
```

Note: MIT App Inventor component names and events can differ slightly depending on which Firebase extension you are using (the built-in FirebaseDB in AI2, or community extensions). The method names above are general — consult the exact component documentation in the Designer/Blocks drawer.

---

## 9. Database structure (example JSON)

```json
{
  "ESP_32": {
    "command": "standby",
    "pump1_status": 0,
    "pump2_status": 0,
    "autoState": 0,
    "response": 42
  }
}
```

---

## 10. Troubleshooting & optimizations


**Commands appear in DB but ESP doesn't respond quickly**

***This Code Uses POLLING Method instead of Interrupt so sometimes it does feel "Laggy" or may miss some commands in some cases.***

* Your sketch polls every 150 ms (set by `checkInterval`). Polling too fast can cause a lot of read operations and may hit limits. Consider:

  * Using the Firebase RTDB streaming/stream listener features (supported by `Firebase_ESP_Client`) for near-instant pushes to the ESP without repeated reads.
  * Increase `checkInterval` slightly and test responsiveness vs. costs.

**Commands flip back instantly in the DB**

* The app might be writing `standby` or another command quickly after. Check your app logic to ensure it only writes what you expect.
* Ensure the ESP only sets `standby` after successfully executing the command.

**Soil reading looks wrong or very noisy**

* Calibrate ADC ranges for your sensor & add smoothing (moving average) or median filtering.
* Remember ESP32 ADC values range about `0..4095` depending on attenuation settings.

**Using ****************************`delay()`**************************** in pumpAuto blocks polls**

* Convert `pumpAuto()` to a non-blocking state-machine if you need to remain responsive to other commands while `auto` is running.

**Checkpoints for debugging**

* Use `Serial.print()` statements liberally to see what the ESP reads/writes.
* Check the Firebase console to see the live RTDB and verify values being written.

**The Same Code can be rewritten to use Interrpt Method Instead of Polling Method to increase Performance.
   this can be done by using the inbuilt timers of esp32, Writing ISR of Command reading and other Activities. Another Major Optimisation would be to read commands from firebase only when the firebase detects any changes (currently we poll for the command in firebase continuously for every set delay)**

---

## 11. Security & deployment notes

* **Do not publish your API key or database secret.** Treat them like credentials. For public repositories, remove secrets and use environment variables or instructions for users to insert their own keys.
* Legacy database secrets are deprecated. Use Firebase Authentication and secure rules to restrict who can write/read. For device-to-database trust you can use Cloud Functions or custom tokens instead of embedding secrets in firmware.
* Monitor read/write usage and bandwidth in Firebase dashboard to avoid hitting free-tier limits.

---

## 12. FAQ

**Q: Why does the ESP set ****************************`standby`**************************** after executing a command?**
A: It’s a simple way to indicate that the command was consumed and to avoid repeatedly performing the same action when the call is polled again.

**Q: How to make the app instantly reflect pump states?**
A: Ensure the ESP updates `/ESP_32/pump1_status` and `/ESP_32/pump2_status` immediately after changing outputs. Have the app either poll those tags or listen for changes.

**Q: Can I reduce latency and Firebase usage?**
A: Use RTDB streaming (push-based) listeners on the ESP so the database sends updates only when values change. Convert blocking delays to `millis()`-based non-blocking logic.

Q: Why use analogWrite instead of ledc functions ?

A: On the ESP32 the preferred, native way to generate PWM is the LEDC API (`ledcSetup`, `ledcAttachPin`, `ledcWrite`) because it's hardware PWM and offers better control (frequency, resolution, multiple channels). However, in this project **`analogWrite()`**** was used as a deliberate workaround** for the following reason:

* Despite multiple attempts to update/reinstall the ESP32 board package and related libraries, the LEDC/`ledc` functions would not compile on the developer's environment. That prevented successful building of the app.
