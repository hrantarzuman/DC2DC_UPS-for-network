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
// Kesinti başladığında NTP ile alınan gerçek takvim zamanı (bkz. GMT_OFFSET_SEC)
// kaydedilir; hem aktif kesintinin süresi/başlangıç saati hem de RAM'de tutulan
// son ~30 kesintinin geçmişi (/gecmis komutu, HTTP sayfası) buradan gelir.
//
// GEREKLİ KÜTÜPHANE: yok — sadece ESP32 Arduino core (WiFi.h, HTTPClient.h,
// WiFiClientSecure.h, WebServer.h, Preferences.h, ArduinoOTA.h dahili gelir). Arduino
// IDE'de board olarak "ESP32C3 Dev Module" (Boards Manager: "esp32" by Espressif
// Systems) seçin.
//
// OTA (KABLOSUZ) GÜNCELLEME: kart WiFi'ye bağlandıktan sonra Arduino IDE'de
// Tools > Port altında "UPS_ESP32C3 at <ip>" adıyla görünür — USB'ye gerek kalmadan
// oradan yeni firmware yüklenebilir. secrets.h'deki OTA_PASSWORD ile korumalıdır.
// İLK kurulum yine de USB ile yapılmalı (bu OTA desteğini içeren firmware kartta
// olana kadar OTA çalışmaz) — bundan sonraki tüm güncellemeler OTA ile yapılabilir.
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
//   EN KOLAY YOL: http://<esp32-ip>/kalibrasyon sayfasından — multimetre ile gerçek
//   voltajı ölçüp girin, oran otomatik hesaplanıp NVS'ye (kalıcı hafıza) yazılır,
//   yeniden flaş atmaya gerek YOK.
//   Alternatif (seri port ile, ilk kurulumda kod içi varsayılanı değiştirmek isterseniz):
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
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "secrets.h"

// WiFi üzerinden (OTA) firmware güncellemesi: Arduino IDE'de Tools > Port altında
// "UPS_ESP32C3 at <ip>" olarak görünür, USB'ye gerek kalmadan oradan yükleme yapılabilir.
// Şifre secrets.h'deki OTA_PASSWORD'den gelir.
const char* OTA_HOSTNAME = "UPS_ESP32C3";

// Kalibrasyon oranları (AC/BAT/CHG_DIVIDER_RATIO) artık http://<esp32-ip>/kalibrasyon
// sayfasından, yeniden flaş atmaya gerek kalmadan ayarlanabiliyor — girilen değer
// ESP32'nin kalıcı hafızasına (NVS) yazılır, resetlense/kesinti olsa bile kaybolmaz.
// Kodun başındaki AC_DIVIDER_RATIO/BAT_DIVIDER_RATIO/CHG_DIVIDER_RATIO sabitleri sadece
// NVS boşsa (ilk kurulum) kullanılan başlangıç değerleridir.
Preferences calibPrefs;

// Kesinti başlangıç saatini/tarihini gerçek takvim zamanı olarak gösterebilmek için
// NTP ile zaman senkronize edilir (ESP32'nin pilli RTC'si yok, WiFi bağlanınca
// internetten çekilir). Türkiye UTC+3, DST (yaz saati) uygulamıyor.
const long GMT_OFFSET_SEC = 3 * 3600;
const int DAYLIGHT_OFFSET_SEC = 0;
const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.google.com";

const int PIN_VAC = 0;
const int PIN_VBAT = 1;
const int PIN_VCHG = 3;
const int PIN_LED_GREEN = 6;
const int PIN_LED_RED = 7;

// Yerel ağdaki herhangi bir tarayıcıdan (telefon dahil) http://<esp32-ip>/ ile
// erişilen basit durum sayfası. IP adresi Wi-Fi bağlanınca seri porta basılır.
WebServer webServer(80);
float gVac = 0, gVbat = 0, gVchg = 0;
float gVacRawMv = 0, gVbatRawMv = 0, gVchgRawMv = 0;
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

// Aktif kesinti takibi: outageStartMillis == 0 iken kesinti yok demektir. Kesinti
// başladığında (updateState içinde) her ikisi de set edilir, elektrik gelince sıfırlanır.
unsigned long outageStartMillis = 0;
time_t outageStartEpoch = 0;

// Son kesintilerin küçük bir geçmişi — RAM'de tutulur (ESP32 resetlenirse kaybolur,
// ama UPS'in kendisi kesinti sırasında hiç kapanmadığı için pratikte kaybolmaz).
// Her kayıt ~12 bayt, 30 kayıt ~360 bayt — göz ardı edilebilir bir yer kaplar.
struct OutageRecord { time_t startEpoch; unsigned long durationSec; };
const int OUTAGE_LOG_SIZE = 30;
OutageRecord outageLog[OUTAGE_LOG_SIZE];
int outageLogCount = 0;  // dolu kayıt sayısı (OUTAGE_LOG_SIZE'da sabitlenir)
int outageLogNext = 0;   // bir sonraki yazılacak (dairesel) index

void logOutage(time_t startEpoch, unsigned long durationSec) {
  outageLog[outageLogNext] = {startEpoch, durationSec};
  outageLogNext = (outageLogNext + 1) % OUTAGE_LOG_SIZE;
  if (outageLogCount < OUTAGE_LOG_SIZE) outageLogCount++;
}

// Gercek zaman NTP ile senkronize olmadan once time() kucuk/anlamsiz bir deger doner.
bool timeIsSynced() {
  return time(nullptr) > 1700000000;  // yaklasik 2023'ten sonraysa senkronize kabul edilir
}

String formatDuration(unsigned long totalMs) {
  unsigned long totalSec = totalMs / 1000;
  unsigned long h = totalSec / 3600;
  unsigned long m = (totalSec % 3600) / 60;
  unsigned long s = totalSec % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

String formatEpoch(time_t t) {
  if (t < 1700000000) return "bilinmiyor (zaman senkronize degil)";
  struct tm timeinfo;
  localtime_r(&t, &timeinfo);
  char buf[32];
  strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M:%S", &timeinfo);
  return String(buf);
}

// En yeniden eskiye, en fazla 10 kesinti kaydını okunabilir bir mesaja çevirir.
String buildOutageLogMessage() {
  if (outageLogCount == 0) return "Henuz kayitli kesinti yok.";
  String msg = "Son kesintiler (en yeniden eskiye):\n";
  int shown = 0;
  const int maxShow = 10;
  for (int i = 0; i < outageLogCount && shown < maxShow; i++) {
    int idx = (outageLogNext - 1 - i + OUTAGE_LOG_SIZE) % OUTAGE_LOG_SIZE;
    msg += formatEpoch(outageLog[idx].startEpoch) + " - sure: " +
           formatDuration(outageLog[idx].durationSec * 1000UL) + "\n";
    shown++;
  }
  return msg;
}

enum UpsState { STATE_AC_OK, STATE_ON_BATTERY, STATE_ON_BATTERY_LOW };
UpsState currentState = STATE_AC_OK;
UpsState lastReportedState = STATE_AC_OK;

// Arduino IDE, .ino dosyaları için fonksiyon prototiplerini otomatik olarak dosyanın
// başına (bu enum'dan ÖNCEYE) ekler. stateName() parametresi UpsState olduğundan bu
// otomatik prototip enum tanımlanmadan önce oluşup derleme hatası veriyordu. Burada
// doğru prototipi elle vererek Arduino'nun kendi (hatalı) prototipini eklemesini
// engelliyoruz.
const char* stateName(UpsState s);

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
      // Kesinti ilk kez onaylandı: gerçek başlangıç anı acLostSince (debounce'tan
      // ÖNCEki an) olduğundan, geriye doğru hesaplayıp o anın takvim zamanını buluyoruz.
      if (outageStartMillis == 0) {
        outageStartMillis = acLostSince;
        outageStartEpoch = time(nullptr) - (long)((now - acLostSince) / 1000);
      }

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
      if (currentState != STATE_AC_OK && outageStartMillis != 0) {
        unsigned long durationSec = (now - outageStartMillis) / 1000;
        logOutage(outageStartEpoch, durationSec);
        outageStartMillis = 0;
      }
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
    if (outageStartMillis != 0) {
      msg += "\nKesinti baslangici: " + formatEpoch(outageStartEpoch) +
             "\nKesinti suresi: " + formatDuration(millis() - outageStartMillis);
    }
    sendTelegramMessage(msg);
  } else if (text == "/gecmis" || text == "/log") {
    sendTelegramMessage(buildOutageLogMessage());
  } else if (text == "/start") {
    sendTelegramMessage("UPS izleme botu aktif. Komutlar:\n"
                         "/durum - anlik AC/batarya durumu (kesinti varsa suresiyle)\n"
                         "/gecmis - son kesintilerin listesi");
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
    case STATE_AC_OK: {
      msg = "UPS: Elektrik geldi, mains'e donuldu. Batarya: " + String(soc) + "%";
      if (outageLogCount > 0) {
        int idx = (outageLogNext - 1 + OUTAGE_LOG_SIZE) % OUTAGE_LOG_SIZE;
        msg += "\nKesinti baslangici: " + formatEpoch(outageLog[idx].startEpoch) +
               "\nKesinti suresi: " + formatDuration(outageLog[idx].durationSec * 1000UL);
      }
      break;
    }
    case STATE_ON_BATTERY:
      msg = "UPS: ELEKTRIK KESILDI! Batarya ile calisiyor. Batarya: " + String(soc) +
            "% (" + String(vBat, 2) + "V)" +
            "\nKesinti baslangici: " + formatEpoch(outageStartEpoch);
      break;
    case STATE_ON_BATTERY_LOW:
      msg = "UPS: DUSUK BATARYA UYARISI! Kesinti devam ediyor, batarya: " + String(soc) +
            "% (" + String(vBat, 2) + "V) - kalan sure kisitli olabilir." +
            "\nKesinti baslangici: " + formatEpoch(outageStartEpoch) +
            "\nKesinti suresi: " + formatDuration(millis() - outageStartMillis);
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
  if (outageStartMillis != 0) {
    html += "<div class='row'>Kesinti baslangici: " + formatEpoch(outageStartEpoch) + "</div>";
    html += "<div class='row'>Kesinti suresi: " + formatDuration(millis() - outageStartMillis) + "</div>";
  }
  html += "<div class='row'>Uptime: " + String(millis() / 1000) + " s</div>";
  html += "<h2>Son kesintiler</h2><div class='row' style='white-space:pre-line'>" +
          buildOutageLogMessage() + "</div>";
  html += "<p><a href='/kalibrasyon' style='color:#8ab4f8'>Kalibrasyon</a></p>";
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

// http://<esp32-ip>/kalibrasyon — multimetreyle ölçülen gerçek voltajı girip
// AC/BAT/CHG_DIVIDER_RATIO'yu yeniden flaş atmadan, doğrudan buradan güncellemek için.
void handleCalibration() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Kalibrasyon</title>"
                "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:20px}"
                "h2{margin-top:1.5em}.info{color:#aaa;font-size:0.95em}"
                "input{font-size:1em;padding:4px;width:100px}"
                "button{font-size:1em;padding:4px 12px;margin-left:6px}"
                "a{color:#8ab4f8}</style></head><body>";
  html += "<p><a href='/'>&larr; Durum sayfasina don</a></p>";
  html += "<h1>Kalibrasyon</h1>";
  html += "<p class='info'>Multimetre ile hattin GERCEK voltajini olcup asagiya girin ve "
           "Kaydet'e basin. Oran otomatik hesaplanip kalici hafizaya (NVS) yazilir, "
           "reset/kesinti ile kaybolmaz, yeniden flas atmaya gerek yok.</p>";

  struct Line { const char* baslik; const char* hat; float rawMv; float voltaj; float oran; };
  Line lines[3] = {
    {"AC hatti", "ac", gVacRawMv, gVac, AC_DIVIDER_RATIO},
    {"Batarya", "bat", gVbatRawMv, gVbat, BAT_DIVIDER_RATIO},
    {"Sarj cikisi", "chg", gVchgRawMv, gVchg, CHG_DIVIDER_RATIO},
  };
  for (int i = 0; i < 3; i++) {
    html += "<h2>" + String(lines[i].baslik) + "</h2>";
    html += "<p class='info'>Ham deger: " + String(lines[i].rawMv, 1) + " mV &nbsp; | &nbsp; "
            "Hesaplanan: " + String(lines[i].voltaj, 2) + " V &nbsp; | &nbsp; "
            "Mevcut oran: " + String(lines[i].oran, 5) + "</p>";
    html += "<form method='POST' action='/kalibrasyon/kaydet'>";
    html += "<input type='hidden' name='hat' value='" + String(lines[i].hat) + "'>";
    html += "<label>Gercek voltaj (V): <input type='number' step='0.01' name='gercek' required></label>";
    html += "<button type='submit'>Kaydet</button>";
    html += "</form>";
  }
  html += "</body></html>";
  webServer.send(200, "text/html", html);
}

void handleCalibrationSave() {
  String hat = webServer.arg("hat");
  float gercek = webServer.arg("gercek").toFloat();

  float rawMv = 0;
  const char* key = nullptr;
  if (hat == "ac") { rawMv = gVacRawMv; key = "ac_ratio"; }
  else if (hat == "bat") { rawMv = gVbatRawMv; key = "bat_ratio"; }
  else if (hat == "chg") { rawMv = gVchgRawMv; key = "chg_ratio"; }

  // rawMv çok küçükse (örn. sensör hattı bağlı değil) sıfıra bölme/anlamsız dev sayı olmasın
  if (key != nullptr && rawMv > 10.0 && gercek > 0) {
    float newRatio = gercek / (rawMv / 1000.0);
    calibPrefs.putFloat(key, newRatio);
    if (hat == "ac") AC_DIVIDER_RATIO = newRatio;
    else if (hat == "bat") BAT_DIVIDER_RATIO = newRatio;
    else if (hat == "chg") CHG_DIVIDER_RATIO = newRatio;
  }

  webServer.sendHeader("Location", "/kalibrasyon");
  webServer.send(303);
}

void setup() {
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  Serial.begin(115200);
  delay(500);
  Serial.println("UPS_MONITOR_ESP32C3_BOOT");
  analogReadResolution(12);

  // Kalibrasyon oranlarını NVS'den yükle — hiç kaydedilmemişse (ilk kurulum) kodun
  // başındaki AC_DIVIDER_RATIO/BAT_DIVIDER_RATIO/CHG_DIVIDER_RATIO değerleri kalır.
  calibPrefs.begin("ups", false);
  AC_DIVIDER_RATIO = calibPrefs.getFloat("ac_ratio", AC_DIVIDER_RATIO);
  BAT_DIVIDER_RATIO = calibPrefs.getFloat("bat_ratio", BAT_DIVIDER_RATIO);
  CHG_DIVIDER_RATIO = calibPrefs.getFloat("chg_ratio", CHG_DIVIDER_RATIO);

  connectWifi();
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);

  // Boot öncesi bekleyen eski Telegram komutlarını sessizce temizle — aksi halde her
  // yeniden başlatmada eski bir "/durum" komutuna gecikmeli yanıt gider.
  syncTelegramOffset();

  webServer.on("/", handleRoot);
  webServer.on("/kalibrasyon", handleCalibration);
  webServer.on("/kalibrasyon/kaydet", HTTP_POST, handleCalibrationSave);
  webServer.begin();
  Serial.println("HTTP durum sayfasi baslatildi (yukarida basilan IP adresine tarayicidan gidin)");

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("OTA guncelleme basladi"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA guncelleme tamamlandi, yeniden baslatiliyor"); });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.print("OTA hata, kod: ");
    Serial.println(error);
  });
  ArduinoOTA.begin();
  Serial.println("OTA hazir - Arduino IDE > Tools > Port altinda 'UPS_ESP32C3 at <ip>' gorunmeli");
}

void loop() {
  ArduinoOTA.handle();

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
  gVacRawMv = vAcRawMv;
  gVbatRawMv = vBatRawMv;
  gVchgRawMv = vChgRawMv;

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
