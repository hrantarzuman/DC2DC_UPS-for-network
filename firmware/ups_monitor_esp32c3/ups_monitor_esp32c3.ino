// UPS İzleme Firmware'i — ESP32-C3 Mini (SuperMini)
//
// ÖNEMLİ MİMARİ NOTU: Bu firmware güç yolunu KONTROL ETMEZ. Mains/batarya geçişi
// tamamen pasif donanımla (Schottky diyot ORing) yapılır — bir yazılım hatası asla
// modemin/Pi'nin gücünü kesemez. Firmware'in tek görevi: durumu izlemek, LED ile
// göstermek, durum değişikliklerinde Wi-Fi üzerinden doğrudan Telegram'a bildirim
// göndermek — Pi'nin ayakta olmasına bağımlı DEĞİLDİR (Pi çökse/ağdan düşse bile
// bildirim gider, çünkü modem zaten bu UPS tarafından besleniyor ve Wi-Fi ayakta kalıyor) —
// ve Telegram'dan gelen "/durum" komutuna anlık AC/batarya durumuyla cevap vermek.
// Ayrıca yerel ağdaki herhangi bir cihazdan (telefon dahil) http://<esp32-ip>/
// adresiyle görüntülenebilen basit bir durum sayfası sunar (bkz. WebServer).
//
// GEREKLİ KÜTÜPHANE: yok — sadece ESP32 Arduino core (WiFi.h, HTTPClient.h,
// WiFiClientSecure.h, WebServer.h dahili gelir). Arduino IDE'de board olarak
// "ESP32C3 Dev Module" (Boards Manager: "esp32" by Espressif Systems) seçin.
//
// SIR YÖNETİMİ: secrets.h.example dosyasını "secrets.h" olarak kopyalayıp kendi
// Wi-Fi/Telegram bilgilerinizi girin. secrets.h .gitignore'da — asla GitHub'a gitmez.
//
// PIN SEÇİMİ NEDENİ (ESP32-C3 için önemli): GPIO2, GPIO8, GPIO9 boot-strapping
// pinleridir — boot sırasında belirli seviyelerde olmaları gerekir, bu yüzden analog
// sense hatları için KULLANILMADI (bu karttaki GPIO9 doğrudan BOOT tuşu, GPIO8
// kartın dahili LED'ine bağlı). Bunun yerine ADC1 kanalları GPIO0/GPIO1/GPIO3 seçildi.
//
// NOT (9 Eylül 2026): 128x64 I2C LCD entegrasyonu denendi (GPIO4/5, sonra GPIO10/20)
// — bir modül arızalı çıktı, ayrıca LCD bağlıyken WiFi bağlanamaz hale geliyordu
// (güç çekişi/EMI şüphesi, bkz. README test notu). LCD'den vazgeçildi, yerine
// aşağıdaki WebServer tabanlı HTTP durum sayfası eklendi — zaten çalışan WiFi
// altyapısını kullandığından ek donanım/kütüphane riski taşımıyor.
//
// Bağlantılar:
//   GPIO0  (ADC1_CH0) -> AC-DC şarj adaptörünün DC çıkışı (~24V), 100kohm(üst)+10kohm(alt) bölücüden sonra
//   GPIO1  (ADC1_CH1) -> kurşun asit akü artı ucu (=ortak bara), 47kohm(üst)+10kohm(alt) bölücüden sonra
//   GPIO3  (ADC1_CH3) -> şarj devresinin diyottan ÖNCEKİ çıkışı, 47kohm(üst)+10kohm(alt) bölücüden sonra
//                         (akü şarj olurken buradaki gerilimi izlemek için — GPIO2'YE DEĞİL, o strapping pini)
//   GPIO6  -> 2 renkli LED'in YEŞİL anodu (+ 220-330ohm direnç)
//   GPIO7  -> 2 renkli LED'in KIRMIZI anodu (+ 220-330ohm direnç)
//            LED'in ORTAK bacağı -> GND (ORTAK KATOT varsayıldı — kurulumdan önce
//            multimetrenin diyot-test moduyla doğrulayın: siyah prob ortada, kırmızı
//            prob dış bacakta iken LED yanıyorsa ortak katottur. Yanmıyorsa LED'iniz
//            ORTAK ANOT'tur — bu durumda ortak bacağı GND yerine 3.3V'a bağlayın VE
//            aşağıdaki ledOn()/ledOff() fonksiyonlarındaki HIGH/LOW değerlerini
//            ters çevirin.
//   USB-C            -> sadece güç ve programlama için (ayrı bir 5V kaynaktan beslenecek, BOM'a bakın)
//
// KALİBRASYON: ✅ 9 Eylül 2026'da yapıldı (bkz. AC_DIVIDER_RATIO/BAT_DIVIDER_RATIO
// tanımlarındaki not). Farklı bir kart/direnç seti kullanırsanız tekrarlayın:
//   1. Multimetre ile AC-DC adaptörün gerçek çıkış voltajını ölçün.
//   2. Seri port monitöründe (115200 baud) basılan "VAC_RAW_MV" değerini okuyun.
//   3. AC_DIVIDER_RATIO'yu gerçek_voltaj / (VAC_RAW_MV/1000) olacak şekilde güncelleyin.
//   4. Aynısını batarya için VBAT_RAW_MV ve BAT_DIVIDER_RATIO ile tekrarlayın.
//   Direnç toleransı (%1-5) ve ESP32 ADC'nin bilinen doğrusalsızlığı yüzünden teorik
//   oranlar birebir tutmaz — bu kalibrasyon adımı AVR'dekinden daha da önemlidir.

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "secrets.h"

const int PIN_VAC = 0;
const int PIN_VBAT = 1;
const int PIN_VCHG = 3;
const int PIN_LED_GREEN = 6;
const int PIN_LED_RED = 7;

// Yerel ağdaki herhangi bir tarayıcıdan (telefon dahil) http://<esp32-ip>/ ile
// erişilen basit durum sayfası. IP adresi Wi-Fi bağlanınca seri porta basılır.
WebServer webServer(80);
float gVac = 0, gVbat = 0, gVchg = 0;
int gSoc = 0;

// Ortak katot varsayıldı: HIGH = LED yanar. Ortak anot ise bu ikisini ters çevirin.
const int LED_ON = HIGH;
const int LED_OFF = LOW;

const int ADC_SAMPLES = 32;       // ESP32 ADC gürültülü olduğundan ortalama alınır
const float ADC_MAX_MV = 3300.0;  // 12-bit, 11dB attenuation ile tam skala ~3.3V
const int ADC_MAX_COUNT = 4095;

// KALİBRE EDİLDİ (9 Eylül 2026) — multimetre referansı: adaptör 24.0V, akü mains
// varken 14.0V, mains yokken 13.6V. Firmware'in o anda gösterdiği (teorik oranla
// hesaplanmış) VAC/VBAT değerleriyle karşılaştırılıp NEW = OLD × (gerçek/gösterilen)
// formülüyle hesaplandı. Direnç toleransı + ESP32 ADC doğrusalsızlığı yüzünden
// teorik (100k+10k=11.0, 47k+10k=5.7) değerlerden belirgin sapma normaldi.
float AC_DIVIDER_RATIO = 9.83;
float BAT_DIVIDER_RATIO = 5.08427;

// GPIO3 şarj bölücüsü BAT ile aynı direnç çiftini kullanıyor (47k+10k), bu yüzden
// başlangıç değeri olarak BAT_DIVIDER_RATIO kopyalandı — ama farklı bir fiziksel
// düğüm (şarj devresinin diyottan önceki çıkışı) olduğundan AYRI kalibre edilmeli:
// VCHG_RAW_MV'yi serial monitörden okuyup gerçek multimetre değeriyle karşılaştırın.
float CHG_DIVIDER_RATIO = 5.16623;

// Mains kaybı algılama eşiği: adaptör ~24V, ~10V altına düşerse "kayıp" kabul edilir
const float AC_LOST_THRESHOLD_V = 10.0;
const unsigned long DEBOUNCE_MS = 2000;

// 12V kurşun asit (Power-Xtra PX26-12B, resmi datasheet 9 Eylül 2026) için kaba
// voltaj->SOC tablosu. ÖNEMLİ SINIRLAMA: üretici açık-devre voltaj->SOC tablosu
// yayınlamıyor (kurşun asitte bu değer yüke/sıcaklığa çok bağımlı olduğundan yaygın
// değildir). 0% ucu datasheet'in RESMİ 20 saatlik deşarj bitiş voltajından (10.50V)
// alındı, 100% ucu endüstri standardı tam-dolu dinlenme voltajı (~12.7V). Ayrıca:
// mains kesildiğinde akü voltajı birkaç dakika boyunca gerçekte olduğundan DAHA
// DÜŞÜK görünebilir ("surface charge" / yüzey şarjının hızla dağılması) — bu
// gerçek kapasite kaybı değildir, kendi kendine düzelir. Bu yüzden ON_BATTERY_LOW
// geçişi ayrıca LOW_BATTERY_DEBOUNCE_MS ile sürekliliği doğrulanır (aşağıya bakın),
// tek bir anlık düşük okumayla alarm tetiklenmez.
struct SocPoint { float voltage; int soc; };
const SocPoint SOC_TABLE[] = {
  {12.70, 100}, {12.40, 85}, {12.20, 65}, {12.00, 45},
  {11.80, 30},  {11.60, 18}, {11.40, 10}, {10.50, 0}
};
const int SOC_TABLE_SIZE = sizeof(SOC_TABLE) / sizeof(SOC_TABLE[0]);
const int SOC_LOW_THRESHOLD = 25;  // ~11.7V civarına denk gelir, 10.50V gerçek tabana iyi bir marj bırakır
const unsigned long LOW_BATTERY_DEBOUNCE_MS = 60000;  // düşük SOC 1 dakika sürmeden LOW'a geçilmez

enum UpsState { STATE_AC_OK, STATE_ON_BATTERY, STATE_ON_BATTERY_LOW };
UpsState currentState = STATE_AC_OK;
UpsState lastReportedState = STATE_AC_OK;

bool pendingAcLost = false;
unsigned long acLostSince = 0;
bool pendingAcRestored = false;
unsigned long acRestoredSince = 0;

bool pendingLow = false;
unsigned long lowSince = 0;
bool pendingRecovered = false;
unsigned long recoveredSince = 0;

unsigned long lastReportMs = 0;
const unsigned long REPORT_INTERVAL_MS = 5000;
unsigned long lastBlinkMs = 0;
bool ledOn = false;

// Telegram komut dinleme (getUpdates polling)
long lastUpdateId = 0;
unsigned long lastPollMs = 0;
const unsigned long POLL_INTERVAL_MS = 4000;

int readAveraged(int pin) {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(pin);
    delayMicroseconds(200);
  }
  return sum / ADC_SAMPLES;
}

// Bölücüden ÖNCEKİ, ESP32 pininin gördüğü ham gerilim (mV) — kalibrasyonda kullanılır.
float readRawMv(int pin) {
  int raw = readAveraged(pin);
  return (raw / (float)ADC_MAX_COUNT) * ADC_MAX_MV;
}

float voltageFromRawMv(float rawMv, float dividerRatio) {
  return (rawMv / 1000.0) * dividerRatio;
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
      // Pil moduna geçildi (veya zaten pil modundayız). ON_BATTERY_LOW'a geçiş
      // ayrıca kendi debounce'ından geçer — mains kesilir kesilmez akünün
      // "surface charge"ı hızla dağıldığı için voltaj birkaç dakika gerçekte
      // olduğundan düşük görünebilir; bu tek başına alarm tetiklememeli.
      bool lowNow = (soc <= SOC_LOW_THRESHOLD);
      if (lowNow) {
        if (!pendingLow) { pendingLow = true; lowSince = now; }
        pendingRecovered = false;
        if (now - lowSince >= LOW_BATTERY_DEBOUNCE_MS) {
          currentState = STATE_ON_BATTERY_LOW;
        } else if (currentState != STATE_ON_BATTERY_LOW) {
          currentState = STATE_ON_BATTERY;
        }
      } else {
        if (!pendingRecovered) { pendingRecovered = true; recoveredSince = now; }
        pendingLow = false;
        if (currentState == STATE_ON_BATTERY_LOW) {
          if (now - recoveredSince >= LOW_BATTERY_DEBOUNCE_MS) currentState = STATE_ON_BATTERY;
        } else {
          currentState = STATE_ON_BATTERY;
        }
      }
    }
  } else {
    if (!pendingAcRestored) { pendingAcRestored = true; acRestoredSince = now; }
    pendingAcLost = false;
    pendingLow = false;
    pendingRecovered = false;
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

// httpGetString() her çağrıldığında (poll, bildirim denemesi/tekrar denemesi) WiFi
// kopuksa bu fonksiyonu çağırır. Önceki deneme ESP-IDF içinde tam çözülmeden yenisi
// başlatılırsa sürücü "cannot set config" hatası verip kilitlenebiliyor — bu yüzden
// art arda çağrılara karşı bir soğuma süresi var; zaten bağlıysa hiçbir şey yapmaz.
unsigned long lastWifiAttemptMs = 0;
const unsigned long WIFI_RETRY_COOLDOWN_MS = 10000;

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (lastWifiAttemptMs != 0 && now - lastWifiAttemptMs < WIFI_RETRY_COOLDOWN_MS) return;
  lastWifiAttemptMs = now;

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

// Genel HTTPS GET yardımcı fonksiyonu — hem sendMessage hem getUpdates için kullanılır.
// Başarılıysa yanıt gövdesini (JSON metni) döner, başarısızsa boş string döner.
String httpGetString(const String& url) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    if (WiFi.status() != WL_CONNECTED) return "";
  }

  WiFiClientSecure client;
  client.setInsecure();  // sertifika doğrulaması atlanır — hobi projesi için kabul edilebilir
  HTTPClient http;
  String body = "";

  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode == 200) {
      body = http.getString();
    }
    http.end();
  }
  return body;
}

bool sendTelegramMessage(const String& text) {
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) +
               "&text=" + urlEncode(text);
  String resp = httpGetString(url);
  bool ok = resp.length() > 0;
  Serial.print("Telegram gonderim sonucu: ");
  Serial.println(ok ? "OK" : "HATA");
  return ok;
}

// JSON metninde "key":değer kalıbını arayıp değeri döner (basit, kütüphanesiz ayrıştırma).
// Telegram'ın getUpdates yanıtı sabit bir formatta olduğundan bu yeterli.
String extractJsonValue(const String& json, const String& key, bool isString) {
  String pattern = "\"" + key + "\":";
  int idx = json.lastIndexOf(pattern);
  if (idx == -1) return "";
  int start = idx + pattern.length();
  if (isString) {
    start = json.indexOf('"', start) + 1;
    int end = json.indexOf('"', start);
    if (start == 0 || end == -1) return "";
    return json.substring(start, end);
  } else {
    int end = start;
    while (end < (int)json.length() && (isDigit(json[end]) || json[end] == '-')) end++;
    return json.substring(start, end);
  }
}

// Boot sırasında bekleyen eski komutları TEK adımda temizler: offset=-1, Telegram'ın
// kuyruğundaki EN SON güncellemeyi ister; lastUpdateId'yi ona göre ayarlamak, ondan
// eski her şeyi (varsa birikmiş komutlar dahil) sunucu tarafında da confirm eder.
void syncTelegramOffset() {
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/getUpdates?offset=-1&limit=1&timeout=0";
  String resp = httpGetString(url);
  String updateIdStr = extractJsonValue(resp, "update_id", false);
  if (updateIdStr.length() > 0) {
    lastUpdateId = updateIdStr.toInt();
  }
}

// Telegram'dan gelen /durum komutunu dinler. lastUpdateId'yi her zaman günceller
// (eski komutları tekrar işlememek için), sadece metin "/durum" ise cevap gönderir.
void checkTelegramCommands(float vAc, float vBat, int soc) {
  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/getUpdates?offset=" + String(lastUpdateId + 1) + "&limit=1&timeout=0";
  String resp = httpGetString(url);
  if (resp.length() == 0) return;

  String updateIdStr = extractJsonValue(resp, "update_id", false);
  if (updateIdStr.length() == 0) return;  // bekleyen yeni mesaj yok
  lastUpdateId = updateIdStr.toInt();

  String text = extractJsonValue(resp, "text", true);
  text.trim();
  if (text == "/durum" || text == "/status") {
    String msg = "UPS Durumu:\n" +
                 String("Durum: ") + stateName(currentState) + "\n" +
                 "AC hatti: " + String(vAc, 2) + "V\n" +
                 "Batarya: " + String(vBat, 2) + "V (%" + String(soc) + ")";
    sendTelegramMessage(msg);
  } else if (text == "/start") {
    sendTelegramMessage("UPS izleme botu aktif. Komutlar:\n/durum - anlik AC ve batarya durumu");
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

// Bekleyen (gönderilememiş) bir durum-değişikliği bildirimi varsa başarana kadar
// birkaç saniyede bir tekrar dener — tek seferlik bir HTTPS/TLS aksaklığı yüzünden
// kritik bir "elektrik kesildi" bildirimi sessizce kaybolmasın diye.
bool notifyPending = false;
unsigned long notifyLastAttemptMs = 0;
const unsigned long NOTIFY_RETRY_MS = 5000;

void notifyStateChangeIfNeeded(float vAc, float vBat, int soc) {
  if (currentState != lastReportedState) notifyPending = true;
  if (!notifyPending) return;

  unsigned long now = millis();
  if (notifyLastAttemptMs != 0 && now - notifyLastAttemptMs < NOTIFY_RETRY_MS) return;
  notifyLastAttemptMs = now;

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
  if (sendTelegramMessage(msg)) {
    lastReportedState = currentState;
    notifyPending = false;
  }
}

// Telefon/PC tarayıcısından http://<esp32-ip>/ ile görüntülenen basit durum sayfası.
// 5 saniyede bir otomatik yenilenir (meta refresh) — JavaScript'e gerek yok.
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta http-equiv='refresh' content='5'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>UPS Durumu</title>"
                "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:20px}"
                "h1{font-size:1.4em}.row{margin:10px 0;font-size:1.2em}"
                ".ok{color:#4caf50}.bat{color:#ff9800}.low{color:#f44336}</style></head><body>";
  html += "<h1>UPS Durumu: <span class='" +
          String(currentState == STATE_AC_OK ? "ok" : (currentState == STATE_ON_BATTERY ? "bat" : "low")) +
          "'>" + stateName(currentState) + "</span></h1>";
  html += "<div class='row'>AC hatti: " + String(gVac, 2) + " V</div>";
  html += "<div class='row'>Batarya: " + String(gVbat, 2) + " V (%" + String(gSoc) + ")</div>";
  html += "<div class='row'>Sarj cikisi: " + String(gVchg, 2) + " V</div>";
  html += "<div class='row'>Uptime: " + String(millis() / 1000) + " s</div>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void setup() {
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  Serial.begin(115200);
  delay(500);
  Serial.println("UPS_MONITOR_ESP32C3_BOOT");
  analogReadResolution(12);

  connectWifi();

  // Boot öncesi bekleyen eski Telegram komutlarını sessizce temizle — aksi halde her
  // yeniden başlatmada eski bir "/durum" komutuna gecikmeli yanıt gider.
  syncTelegramOffset();

  webServer.on("/", handleRoot);
  webServer.begin();
  Serial.println("HTTP durum sayfasi baslatildi (yukarida basilan IP adresine tarayicidan gidin)");
}

void loop() {
  float vAcRawMv = readRawMv(PIN_VAC);
  float vBatRawMv = readRawMv(PIN_VBAT);
  float vChgRawMv = readRawMv(PIN_VCHG);
  float vAc = voltageFromRawMv(vAcRawMv, AC_DIVIDER_RATIO);
  float vBat = voltageFromRawMv(vBatRawMv, BAT_DIVIDER_RATIO);
  float vChg = voltageFromRawMv(vChgRawMv, CHG_DIVIDER_RATIO);
  int soc = estimateSoc(vBat);
  gVac = vAc;
  gVbat = vBat;
  gVchg = vChg;
  gSoc = soc;

  updateState(vAc, soc);
  updateLed();
  notifyStateChangeIfNeeded(vAc, vBat, soc);
  webServer.handleClient();

  unsigned long now = millis();
  if (now - lastPollMs >= POLL_INTERVAL_MS) {
    lastPollMs = now;
    checkTelegramCommands(vAc, vBat, soc);
  }

  if (now - lastReportMs >= REPORT_INTERVAL_MS) {
    lastReportMs = now;
    Serial.print("STATE=");
    Serial.print(stateName(currentState));
    Serial.print(";VAC=");
    Serial.print(vAc, 2);
    Serial.print(";VAC_RAW_MV=");
    Serial.print(vAcRawMv, 1);
    Serial.print(";VBAT=");
    Serial.print(vBat, 2);
    Serial.print(";VBAT_RAW_MV=");
    Serial.print(vBatRawMv, 1);
    Serial.print(";VCHG=");
    Serial.print(vChg, 2);
    Serial.print(";VCHG_RAW_MV=");
    Serial.print(vChgRawMv, 1);
    Serial.print(";SOC=");
    Serial.print(soc);
    Serial.print(";WIFI=");
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    Serial.print(";UPTIME_S=");
    Serial.println(now / 1000);
  }
}
