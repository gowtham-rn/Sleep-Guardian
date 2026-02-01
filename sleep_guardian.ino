/***************************************************
 *  Sleep Guardian – Smart Sleep Monitoring System
 *  Board  : ESP32
 *  Sensor : MAX30100
 *  Cloud  : Blynk IoT
 *
 *  NOTE:
 *  - Replace WiFi credentials & Blynk Auth Token
 *  - Do NOT expose real tokens on GitHub
 ***************************************************/

#define BLYNK_TEMPLATE_ID   "TMPL3Km5CfU3Y"
#define BLYNK_TEMPLATE_NAME "MAX30100 Pulse Oximeter"
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN"   

#include <Wire.h>
#include "MAX30100_PulseOximeter.h"
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// -------------------- WiFi Credentials --------------------
char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

// -------------------- Objects --------------------
PulseOximeter pox;

// -------------------- Timers & Flags --------------------
unsigned long tsLastReport = 0;
unsigned long fingerDetectedTime = 0;
bool fingerPlaced = false;

// -------------------- Blynk Virtual Pins --------------------
#define VIRTUAL_HR     V0
#define VIRTUAL_SPO2   V1
#define V2_DISEASE     V2
#define V3_STAGE       V3
#define V4_AVGHR       V4

// -------------------- HR Average --------------------
float hrSum = 0;
int hrCount = 0;

// -------------------- Alert Timers --------------------
unsigned long lowHR_start = 0;
unsigned long highHR_start = 0;

// -------------------- Sleep Timer --------------------
unsigned long sleepStart = 0;
bool sleepTimerStarted = false;

// -------------------- Reconnect Timers --------------------
unsigned long wifiRetryTimer = 0;
unsigned long blynkRetryTimer = 0;


// -------------------- Non-Blocking WiFi --------------------
void connectWiFiNonBlocking() {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.print("WiFi Lost");

    if (millis() - wifiRetryTimer > 3000) {
      WiFi.begin(ssid, pass);
      wifiRetryTimer = millis();
    }
  }
}

// -------------------- Non-Blocking Blynk --------------------
void connectBlynkNonBlocking() {
  if (!Blynk.connected()) {
    lcd.clear();
    lcd.print("Blynk Lost");

    if (millis() - blynkRetryTimer > 3000) {
      Blynk.connect();
      blynkRetryTimer = millis();
    }
  }
}


// -------------------- Setup --------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();

  // WiFi
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }
  lcd.clear();
  lcd.print("WiFi OK");
  delay(1000);

  // Blynk
  Blynk.config(auth);
  while (!Blynk.connect()) {
    delay(300);
  }
  lcd.clear();
  lcd.print("Blynk OK");
  delay(1000);

  // Sensor
  if (!pox.begin()) {
    lcd.clear();
    lcd.print("Sensor Error!");
    while (1);
  }

  pox.setIRLedCurrent(MAX30100_LED_CURR_24MA);

  lcd.clear();
  lcd.print("Place Finger");

  Blynk.virtualWrite(V3_STAGE, "Going on");

  Serial.println("Setup Complete");
}


// -------------------- Extra Logic --------------------
void processExtraLogic(float hr) {

  // Average HR
  if (hr > 40 && hr < 180) {
    hrSum += hr;
    hrCount++;

    float avgHR = hrSum / hrCount;
    Blynk.virtualWrite(V4_AVGHR, avgHR);
  }

  // Bradycardia
  if (hr < 45 && hr > 0) {
    if (!lowHR_start) lowHR_start = millis();

    if (millis() - lowHR_start >= 180000) {
      Blynk.logEvent("bradyalert", "Low Heart Rate Detected");
      lowHR_start = 0;
    }
  } else {
    lowHR_start = 0;
  }

  // Tachycardia
  if (hr > 120) {
    if (!highHR_start) highHR_start = millis();

    if (millis() - highHR_start >= 180000) {
      Blynk.logEvent("tachyalert", "High Heart Rate Detected");
      highHR_start = 0;
    }
  } else {
    highHR_start = 0;
  }

  // Sleep Stage Detection
  if (!sleepTimerStarted && hr > 40) {
    sleepStart = millis();
    sleepTimerStarted = true;
  }

  if (sleepTimerStarted && hrCount > 0) {
    if (millis() - sleepStart >= 5UL * 60UL * 60UL * 1000UL) {

      float finalAvg = hrSum / hrCount;

      if (finalAvg < 55)
        Blynk.virtualWrite(V3_STAGE, "Deep Sleep");
      else if (finalAvg < 65)
        Blynk.virtualWrite(V3_STAGE, "Light Sleep");
      else
        Blynk.virtualWrite(V3_STAGE, "REM Sleep");
    }
  }
}


// -------------------- Loop --------------------
void loop() {

  connectWiFiNonBlocking();
  connectBlynkNonBlocking();

  pox.update();
  if (Blynk.connected()) Blynk.run();

  if (millis() - tsLastReport > 1000) {
    tsLastReport = millis();

    float hr = pox.getHeartRate();
    float spo2 = pox.getSpO2();

    if (hr > 0) {

      if (!fingerPlaced) {
        fingerPlaced = true;
        fingerDetectedTime = millis();
        lcd.clear();
        lcd.print("Stabilizing...");
      }

      if (millis() - fingerDetectedTime > 5000) {

        Blynk.virtualWrite(VIRTUAL_HR, hr);
        Blynk.virtualWrite(VIRTUAL_SPO2, spo2);

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("HR : ");
        lcd.print(hr);

        lcd.setCursor(0, 1);
        lcd.print("SpO2: ");
        lcd.print(spo2);
        lcd.print(" %");

        processExtraLogic(hr);
      }
    }
    else {
      fingerPlaced = false;

      lcd.clear();
      lcd.print("Place Finger");

      Blynk.virtualWrite(VIRTUAL_HR, 0);
      Blynk.virtualWrite(VIRTUAL_SPO2, 0);
    }
  }
}
