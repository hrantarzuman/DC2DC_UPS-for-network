// UPS İzleme Firmware'i — ESP32-C3 Mini (SuperMini)
//
// ÖNEMLİ MİMARİ NOTU: Bu firmware güç yolunu KONTROL ETMEZ. Mains/batarya geçişi
// tamamen pasif donanımla (Schottky diyot ORing) yapılır — bir yazılım hatası asla
// modemin/Pi'nin gücünü kesemez. Firmware'in tek görevi: durumu izlemek, LED ile
// göstermek, ve durum değişikliklerinde Wi-Fi üzerinden doğrudan Telegram'a bildirim
// göndermek — Pi'nin ayakta olmasına bağımlı DEĞİLDİR (Pi çökse/ağdan düşse bile
// bildirim gider, çünkü modem zaten bu UPS tarafından besleniyor ve Wi-Fi ayakta kalıyor).
//
// GEREKLİ KÜTÜPHANE: yok — sadece ESP32 Arduino core (WiFi.h, HTTPClient.h,
// WiFiClientSecure.h dahili gelir). Arduino IDE'de board olarak "ESP32C3 Dev Module"
// (Boards Manager: "esp32" by Espressif Systems) seçin.
//
// SIR YÖNETİMİ: secrets.h.example dosyasını "secrets.h" olarak kopyalayıp kendi
// Wi-Fi/Telegram bilgilerinizi girin. secrets.h .gitignore'da — asla GitHub'a gitmez.
//
// PIN SEÇİMİ NEDENİ (ESP32-C3 için önemli): GPIO2, GPIO8, GPIO9 boot-strapping
// pinleridir — boot sırasında belirli seviyelerde olmaları gerekir, bu yüzden analog
// sense hatları için KULLANILMADI. Bunun yerine ADC1 kanalları (Wi-Fi ile çakışmayan)
// GPIO0 ve GPIO1 seçildi.
//
// Bağlantılar:
//   GPIO0 (ADC1_CH0) -> AC-DC şarj adaptörünün DC çıkışı (~24V), 100kohm(üst)+10kohm(alt) bölücüden sonra
//   GPIO1 (ADC1_CH1) -> kurşun asit akü artı ucu (=ortak bara), 47kohm(üst)+10kohm(alt) bölücüden sonra
//   GPIO6 -> 2 renkli LED'in YEŞİL anodu (+ 220-330ohm direnç)
//   GPIO7 -> 2 renkli LED'in KIRMIZI anodu (+ 220-330ohm direnç)
//            LED'in ORTAK bacağı -> GND (ORTAK KATOT varsayıldı — kurulumdan önce
//            multimetrenin diyot-test moduyla doğrulayın: siyah prob ortada, kırmızı
//            prob dış bacakta iken LED yanıyorsa ortak katottur. Yanmıyorsa LED'iniz
//            ORTAK ANOT'tur — bu durumda ortak bacağı GND yerine 3.3V'a bağlayın VE
//            aşağıdaki ledOn()/ledOff() fonksiyonlarındaki HIGH/LOW değerlerini
//            ters çevirin.
//   USB-C            -> sadece güç ve programlama için (ayrı bir 5V kaynaktan beslenecek, BOM'a bakın)
//
// KALİBRASYON (kurulumdan sonra MUTLAKA yapılmalı):
//   1. Multimetre ile AC-DC adaptörün gerçek çıkış voltajını ölçün.
//   2. Seri port monitöründe (115200 baud) basılan "VAC_RAW_MV" değerini okuyun.
//   3. AC_DIVIDER_RATIO'yu gerçek_voltaj / (VAC_RAW_MV/1000) olacak şekilde güncelleyin.
//   4. Aynısını batarya için VBAT_RAW_MV ve BAT_DIVIDER_RATIO ile tekrarlayın.
//   Direnç toleransı (%1-5) ve ESP32 ADC'nin bilinen doğrusalsızlığı yüzünden teorik
//   oranlar birebir tutmaz — bu kalibrasyon adımı AVR'dekinden daha da önemlidir.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "secrets.h"

const int PIN_VAC = 0;
const int PIN_VBAT = 1;
const int PIN_LED_GREEN = 6;
const int PIN_LED_RED = 7;

// Ortak katot varsayıldı: HIGH = LED yanar. Ortak anot ise bu ikisini ters çevirin.
const int LED_ON = HIGH;
const int LED_OFF = LOW;

const int ADC_SAMPLES = 32;       // ESP32 ADC gürültülü olduğundan ortalama alınır
const float ADC_MAX_MV = 3300.0;  // 12-bit, 11dB attenuation ile tam skala ~3.3V
const int ADC_MAX_COUNT = 4095;

// Teorik başlangıç değerleri — kalibrasyon sonrası güncellenecek
float AC_DIVIDER_RATIO = (100000.0 + 10000.0) / 10000.0;  // ~11.0
float BAT_DIVIDER_RATIO = (47000.0 + 10000.0) / 10000.0;  // ~5.7

// Mains kaybı algılama eşiği: adaptör ~24V, ~10V altına düşerse "kayıp" kabul edilir
const float AC_LOST_THRESHOLD_V = 10.0;
const unsigned long DEBOUNCE_MS = 2000;

// 12V kurşun asit (VRLA) akü için kaba voltaj->SOC tablosu. Not: mains varken bu düğüm
// şarj modülü tarafından 13.6-13.8V'a sabitlendiği için SOC okuması sadece ON_BATTERY
// durumundayken (mains koptuğunda) anlamlıdır. Akünün dahili BMS'i olmadığından
// SOC_LOW_THRESHOLD, LiFePO4'e göre daha erken (daha yüksek) tutuldu — derin deşarj
// (11.5V altı) akünün ömrünü kalıcı olarak kısaltır.
struct SocPoint { float voltage; int soc; };
const SocPoint SOC_TABLE[] = {
  {12.70, 100}, {12.50, 90}, {12.40, 75}, {12.30, 60},
  {12.20, 50},  {12.10, 35}, {12.00, 25}, {11.90, 15},
  {11.80, 5},   {11.50, 0}
};
const int SOC_TABLE_SIZE = sizeof(SOC_TABLE) / sizeof(SOC_TABLE[0]);
const int SOC_LOW_THRESHOLD = 25;

enum UpsState { STATE_AC_OK, STATE_ON_BATTERY, STATE_ON_BATTERY_LOW };
UpsState currentState = STATE_AC_OK;
UpsState lastReportedState = STATE_AC_OK;

bool pendingAcLost = false;
unsigned long acLostSince = 0;
bool pendingAcRestored = false;
unsigned long acRestoredSince = 0;

unsigned long lastReportMs = 0;
const unsigned long REPORT_INTERVAL_MS = 5000;
unsigned long lastBlinkMs = 0;
bool ledOn = false;

int readAveraged(int pin) {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(pin);
    delayMicroseconds(200);
  }
  return sum / ADC_SAMPLES;
}

float readVoltage(int pin, float dividerRatio) {
  int raw = readAveraged(pin);
  float vAdcMv = (raw / (float)ADC_MAX_COUNT) * ADC_MAX_MV;
  return (vAdcMv / 1000.0) * dividerRatio;
}

int estimateSoc(float voltage) {
  if (voltage >= SOC_TABLE[0].voltage) return 100;
  if (voltage <= SOC_TABLE[SOC_TABLE_SIZE - 1].voltage) return 0;
  for (int i = 0; i < SOC_TABLE_SIZE - 1; i++) {
    float vHigh = SOC_TABLE[i].voltage;
    float vLow = SOC_TABLE[i + 1].voltage;
    if (voltage <= vHigh && voltage >= vLow) {
      float frac = (voltage - vLow) / (vHigh - vLow);
      return SOC_TABLE[i + 1].soc + frac * (SOC_TABLE[i].soc - SOC_TABLE[i + 1].soc);
    }
  }
  return 0;
}

const char* stateName(UpsState s) {
  switch (s) {
    case STATE_AC_OK: return "AC_OK";
    case STATE_ON_BATTERY: return "ON_BATTERY";
    case STATE_ON_BATTERY_LOW: return "ON_BATTERY_LOW";
  }
  return "UNKNOWN";
}

void updateState(float vAc, int soc) {
  bool acPresent = vAc >= AC_LOST_THRESHOLD_V;
  unsigned long now = millis();

  if (!acPresent) {
    if (!pendingAcLost) { pendingAcLost = true; acLostSince = now; }
    pendingAcRestored = false;
    if (now - acLostSince >= DEBOUNCE_MS) {
      currentState = (soc <= SOC_LOW_THRESHOLD) ? STATE_ON_BATTERY_LOW : STATE_ON_BATTERY;
    }
  } else {
    if (!pendingAcRestored) { pendingAcRestored = true; acRestoredSince = now; }
    pendingAcLost = false;
    if (now - acRestoredSince >= DEBOUNCE_MS) {
      currentState = STATE_AC_OK;
    }
  }
}

// AC_OK: yeşil sabit yanık. ON_BATTERY: kırmızı yavaş yanıp söner. ON_BATTERY_LOW: kırmızı hızlı yanıp söner.
void updateLed() {
  unsigned long now = millis();
  unsigned long interval;

  switch (currentState) {
    case STATE_AC_OK:
      digitalWrite(PIN_LED_GREEN, LED_ON);
      digitalWrite(PIN_LED_RED, LED_OFF);
      return;
    case STATE_ON_BATTERY:
      digitalWrite(PIN_LED_GREEN, LED_OFF);
      interval = 500;
      break;
    case STATE_ON_BATTERY_LOW:
      digitalWrite(PIN_LED_GREEN, LED_OFF);
      interval = 150;
      break;
    default:
      interval = 1000;
  }

  if (now - lastBlinkMs >= interval) {
    lastBlinkMs = now;
    ledOn = !ledOn;
    digitalWrite(PIN_LED_RED, ledOn ? LED_ON : LED_OFF);
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Wi-Fi baglaniyor");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi baglandi, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi baglanamadi, sonraki dongude tekrar denenecek");
  }
}

void sendTelegramMessage(const String& text) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  WiFiClientSecure client;
  client.setInsecure();  // sertifika doğrulaması atlanır — hobi projesi için kabul edilebilir
  HTTPClient http;

  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) +
               "&text=" + urlEncode(text);

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    Serial.print("Telegram gonderim sonucu: ");
    Serial.println(httpCode);
    http.end();
  }
}

String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char code0, code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

void notifyStateChangeIfNeeded(float vAc, float vBat, int soc) {
  if (currentState == lastReportedState) return;

  String msg;
  switch (currentState) {
    case STATE_AC_OK:
      msg = "UPS: Elektrik geldi, mains'e donuldu. Batarya: " + String(soc) + "%";
      break;
    case STATE_ON_BATTERY:
      msg = "UPS: ELEKTRIK KESILDI! Batarya ile calisiyor. Batarya: " + String(soc) +
            "% (" + String(vBat, 2) + "V)";
      break;
    case STATE_ON_BATTERY_LOW:
      msg = "UPS: DUSUK BATARYA UYARISI! Kesinti devam ediyor, batarya: " + String(soc) +
            "% (" + String(vBat, 2) + "V) - kalan sure kisitli olabilir.";
      break;
  }
  sendTelegramMessage(msg);
  lastReportedState = currentState;
}

void setup() {
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  Serial.begin(115200);
  delay(500);
  Serial.println("UPS_MONITOR_ESP32C3_BOOT");
  analogReadResolution(12);
  connectWifi();
}

void loop() {
  float vAc = readVoltage(PIN_VAC, AC_DIVIDER_RATIO);
  float vBat = readVoltage(PIN_VBAT, BAT_DIVIDER_RATIO);
  int soc = estimateSoc(vBat);

  updateState(vAc, soc);
  updateLed();
  notifyStateChangeIfNeeded(vAc, vBat, soc);

  unsigned long now = millis();
  if (now - lastReportMs >= REPORT_INTERVAL_MS) {
    lastReportMs = now;
    Serial.print("STATE=");
    Serial.print(stateName(currentState));
    Serial.print(";VAC=");
    Serial.print(vAc, 2);
    Serial.print(";VBAT=");
    Serial.print(vBat, 2);
    Serial.print(";SOC=");
    Serial.print(soc);
    Serial.print(";WIFI=");
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    Serial.print(";UPTIME_S=");
    Serial.println(now / 1000);
  }
}
