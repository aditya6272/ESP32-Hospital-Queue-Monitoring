/*
  IoT Based Hospital Queue Monitoring System
  ESP32 + HC-SR04 Ultrasonic Sensor + 16x2 I2C LCD
  + Doctor A/B Buttons + GSM + Blynk IoT

  GitHub-safe version:
  - Wi-Fi password and Blynk token are placeholders.
  - Replace the HC-SR04 pin numbers if your actual wiring is different.
*/

#define BLYNK_TEMPLATE_ID "YOUR_BLYNK_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Hospital Queue Monitoring"
#define BLYNK_AUTH_TOKEN "YOUR_BLYNK_AUTH_TOKEN"

// ------------------- LIBRARIES -------------------
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

// ---------------- WIFI CREDENTIALS ----------------
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

// ----------------- PIN CONFIG ---------------------
// Assumed example wiring because the original circuit diagram
// was not provided. Change these two pins if your wiring differs.
#define TRIG_PIN 18
#define ECHO_PIN 19

#define BUTTON_A 25
#define BUTTON_B 26

// LCD I2C address: commonly 0x27 or 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2);

// GSM (SIM900L/SIM800 on Serial2)
HardwareSerial sim900(2);
#define GSM_RX 16
#define GSM_TX 17

// ----------------- QUEUE SETTINGS -----------------
#define DETECTION_DISTANCE_CM 50
#define SENSOR_DEBOUNCE_MS 2000

int queueCount = 0;
int nowServing = 0;
String currentDoctor = "";

bool patientDetected = false;
unsigned long lastDetectionTime = 0;

// ------------------- SETUP ------------------------
void setup() {
  Serial.begin(115200);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Queue System");
  delay(1000);

  // Ultrasonic sensor
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Buttons
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);

  // GSM
  sim900.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  lcd.clear();
  lcd.print("Init GSM...");
  initGSM();

  // Wi-Fi
  lcd.clear();
  lcd.print("Connecting WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttemptTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected");

    lcd.clear();
    lcd.print("WiFi Connected");
    delay(500);

    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(5000);
  } else {
    Serial.println("\nWiFi Failed");
    lcd.clear();
    lcd.print("WiFi Failed!");
    delay(1000);
  }

  lcd.clear();
  lcd.print("System Ready");
  delay(1000);

  updateDisplay();
  sendToBlynk();
}

// ---------------- ULTRASONIC SENSOR --------------
float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    return -1;
  }

  return duration * 0.0343 / 2.0;
}

// ----------------- LCD UPDATE --------------------
void updateDisplay() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Serv: ");
  lcd.print(nowServing);

  if (currentDoctor.length() > 0) {
    lcd.print(" ");
    lcd.print(currentDoctor);
  }

  lcd.setCursor(0, 1);
  lcd.print("Queue: ");
  lcd.print(queueCount);
}

// ---------------- BLYNK UPDATE -------------------
void sendToBlynk() {
  if (WiFi.status() == WL_CONNECTED && Blynk.connected()) {
    Blynk.virtualWrite(V0, queueCount);
    Blynk.virtualWrite(
      V1,
      String("Token: ") + String(nowServing) + " " + currentDoctor
    );
  }
}

// ---------------- GSM INITIALIZATION -------------
void initGSM() {
  Serial.println("Initializing GSM...");

  sim900.println("AT");
  delay(500);

  sim900.println("AT+CMGF=1");
  delay(500);

  sim900.println("AT+CNMI=1,2,0,0,0");
  delay(500);

  Serial.println("GSM ready.");
}

// ---------------- GSM SMS ------------------------
// Replace YOUR_PHONE_NUMBER with the intended recipient
// only in your private/local copy. Do not publish a real
// personal number in a public repository.
void sendSMS(int token, String doctor) {
  String sms =
    "Now Serving Token " + String(token) + " for " + doctor;

  sim900.println("AT+CMGF=1");
  delay(300);

  sim900.println("AT+CMGS=\"YOUR_PHONE_NUMBER\"");
  delay(300);

  sim900.print(sms);
  sim900.write(26);
  delay(1000);

  Serial.println("SMS command sent: " + sms);
}

// ------------- ASSIGN PATIENT TO DOCTOR ----------
void assignToDoctor(String doctor) {
  if (queueCount > 0) {
    nowServing++;
    currentDoctor = doctor;
    queueCount--;

    updateDisplay();
    sendSMS(nowServing, currentDoctor);
    sendToBlynk();

    Serial.print("Now serving: ");
    Serial.print(nowServing);
    Serial.print(" - ");
    Serial.println(currentDoctor);
  } else {
    Serial.println("No patients waiting.");
  }
}

// ------------------- LOOP -------------------------
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }

  // Read distance from HC-SR04
  float distance = readDistanceCM();

  // Patient detected within configured distance
  if (distance > 0 &&
      distance <= DETECTION_DISTANCE_CM &&
      !patientDetected &&
      millis() - lastDetectionTime >= SENSOR_DEBOUNCE_MS) {

    queueCount++;
    patientDetected = true;
    lastDetectionTime = millis();

    Serial.print("New patient added. Queue = ");
    Serial.println(queueCount);

    updateDisplay();
    sendToBlynk();
  }

  // Patient has moved away from the sensor
  if ((distance < 0 || distance > DETECTION_DISTANCE_CM) &&
      patientDetected) {

    patientDetected = false;
    Serial.println("Sensor cleared. Ready for next patient.");
  }

  // Doctor A button
  if (digitalRead(BUTTON_A) == LOW) {
    assignToDoctor("Dr.A");
    delay(300);
  }

  // Doctor B button
  if (digitalRead(BUTTON_B) == LOW) {
    assignToDoctor("Dr.B");
    delay(300);
  }

  delay(100);
}
