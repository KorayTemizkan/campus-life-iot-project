#include <Adafruit_AHTX0.h>
#include <Wire.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>

#define WIFI_SSID "KORAYKORAY"
#define WIFI_PASSWORD "123456789"

#define FIREBASE_HOST "campuslife-3e1e8-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "PzzxXGbPY4ABm00KXPRmPVZtSoaYdMo6iPoQOIJJ"

FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

Adafruit_AHTX0 aht;

bool manualEnergySaving = false;  // Uygulamadan gelen emir
bool autoEnergySaving = false;    // Sensörlerin hesapladığı durum

const int buzzerPin = D5;       // Buzzer
const int mq135AnalogPin = A0;  // MQ-135
const int mq2DigitalPin = D3;   // MQ-2
const int pirPin = D8;          // PIR

const int redPin = D7;
const int bluePin = D6;
const int greenPin = D4;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 10800);

// =====================
// BUZZER FIX (tone/noTone)
// =====================
bool buzzerIsOn = false;

void buzzerOn(int freq = 2000) {
  if (!buzzerIsOn) {
    tone(buzzerPin, freq);
    buzzerIsOn = true;
  }
}

void buzzerOff() {
  if (buzzerIsOn) {
    noTone(buzzerPin);
    buzzerIsOn = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  timeClient.begin();

  Wire.begin(D2, D1);

  pinMode(buzzerPin, OUTPUT);
  buzzerOff(); // başlangıçta kapalı

  pinMode(redPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(greenPin, OUTPUT);

  pinMode(mq2DigitalPin, INPUT);
  pinMode(pirPin, INPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Wifi baglaniyor!");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWifi baglandi!");

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (!aht.begin()) {
    Serial.println("HATA: AHT10 bulunamadi!");
    while (1) delay(10);
  }

  Serial.println("\n--- CampusLife: Tum Sensorler Aktif ---");
}

void loop() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  timeClient.update();
  int currentHour = timeClient.getHours();

  int airQualityValue = analogRead(mq135AnalogPin);
  int flammableGasAndSmokeValue = digitalRead(mq2DigitalPin);
  int motionValue = digitalRead(pirPin);
  bool isNightTime = (currentHour >= 22 || currentHour < 8);

  Serial.print("Sicaklık: ");
  Serial.print(temp.temperature);
  Serial.print(" C | ");

  Serial.print("Nem: %");
  Serial.print(humidity.relative_humidity);
  Serial.print(" RH | ");

  Serial.print("Hava Seviyesi: ");
  Serial.print(airQualityValue);
  Serial.print(" BR | ");

  Serial.print("Hareket: ");
  Serial.print(motionValue == HIGH ? "VAR" : "YOK");
  Serial.print(" | ");

  Serial.print("yanici gaz: ");
  Serial.print(flammableGasAndSmokeValue == HIGH ? "VAR" : "YOK");
  Serial.print(" | ");
  
  Serial.println("");

  // --- 1. TEHLİKE TESPİTİ ---
  bool danger = (airQualityValue > 800 || flammableGasAndSmokeValue == HIGH ||
                 temp.temperature > 30.0 || humidity.relative_humidity > 80.0 || motionValue == HIGH);

  // --- 2. FIREBASE'DEN KOMUT OKUMA ---
  if (Firebase.getString(firebaseData, "/CampusLife/EnergySaving")) {
    String val = firebaseData.stringData();
    val.replace("\"", "");

    manualEnergySaving = (val == "1");
  }

  // --- 3. TEHLİKE ANINDA TASARRUFU İPTAL ET ---
  if (manualEnergySaving == true && danger) {
    manualEnergySaving = false;
    Firebase.setString(firebaseData, "/CampusLife/EnergySaving", "0");
    Serial.println("!!! TEHLİKE: Tasarruf modu kapatildi ve butona 0 iletildi.");
  }

  // --- 4. IŞIK VE EYLEM KONTROLÜ ---
  if (manualEnergySaving == true) {
    // A) TASARRUF MODU: Yeşil Blink + BUZZER KAPALI
    static bool blinkState = LOW;
    static unsigned long lastBlinkTime = 0;

    if (millis() - lastBlinkTime > 1000) {
      lastBlinkTime = millis();
      blinkState = !blinkState;

      digitalWrite(redPin, LOW);
      digitalWrite(bluePin, LOW);
      digitalWrite(greenPin, blinkState);
    }

    buzzerOff();  // <<< BUZZER FIX
  } else {
    // B) NORMAL MOD: LED seviyesi (buzzer ayrı yönetilecek)
    if (airQualityValue > 500 || flammableGasAndSmokeValue == HIGH) {
      buzzerOn(2000);
      digitalWrite(greenPin, LOW);
      digitalWrite(bluePin, LOW);
      digitalWrite(redPin, HIGH);
    } else if (airQualityValue >= 200) {
      digitalWrite(greenPin, LOW);
      digitalWrite(redPin, LOW);
      digitalWrite(bluePin, HIGH);
    } else {
      digitalWrite(redPin, LOW);
      digitalWrite(bluePin, LOW);
      digitalWrite(greenPin, HIGH);
    }

    // Ekstra Tehlike (Sıcaklık/Nem) -> LED kırmızıya zorla
    if (temp.temperature > 30.0 || humidity.relative_humidity > 80.0) {
      digitalWrite(bluePin, LOW);
      digitalWrite(greenPin, LOW);
      digitalWrite(redPin, HIGH);
    }

    // Hareket ve Güvenlik (Sadece değer değiştiğinde Firebase'e yaz)
    static int lastMotionSent = -1;

    if (motionValue == HIGH) {
      if (isNightTime) {
        digitalWrite(redPin, HIGH);
      }
      if (lastMotionSent != 1) {
        Firebase.setInt(firebaseData, "/CampusLife/MotionValue", 1);
        lastMotionSent = 1;
        digitalWrite(bluePin, LOW);
        digitalWrite(greenPin, LOW);
        digitalWrite(redPin, HIGH);
      }
    } else {
      if (lastMotionSent != 0) {
        Firebase.setInt(firebaseData, "/CampusLife/MotionValue", 0);
        lastMotionSent = 0;
      }
    }

    //buzzer karar
    bool securityAlarm = (motionValue == HIGH); //(isNightTime && motionValue == HIGH)
    bool alarmNow = danger || securityAlarm;

    if (alarmNow) buzzerOn(2000);
    else buzzerOff();
  }

  // Firebase'e temel verileri gönder
  Firebase.setFloat(firebaseData, "/CampusLife/Temperature", temp.temperature);
  Firebase.setFloat(firebaseData, "/CampusLife/RelativeHumidity", humidity.relative_humidity);
  Firebase.setInt(firebaseData, "/CampusLife/AirQualityValue", airQualityValue);
  Firebase.setInt(firebaseData, "/CampusLife/FlammableGasAndSmokeValue", flammableGasAndSmokeValue);

  // Arşiv kayıt
  static unsigned long lastLogTime = 0;
  if (millis() - lastLogTime > 10000) {
    lastLogTime = millis();

    String zaman = timeClient.getFormattedTime();
    String logData =
      zaman + " | Temp:" + String(temp.temperature) +
      " | Hum:%" + String(humidity.relative_humidity) +
      " | Air:" + String(airQualityValue) +
      " | Gas:" + (flammableGasAndSmokeValue == HIGH ? "1" : "0") +
      " | EnergySaving:" + (manualEnergySaving ? "1" : "0");

    Firebase.pushString(firebaseData, "/CampusLife/History", logData);
    Serial.println(">>> Veritabanina Arsiv Kaydi Atildi.");
  }

  delay(100);
}