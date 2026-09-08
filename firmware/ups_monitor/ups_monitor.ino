// UPS İzleme Firmware'i — Arduino Nano
//
// ÖNEMLİ MİMARİ NOTU: Bu firmware güç yolunu KONTROL ETMEZ. Mains/batarya geçişi
// tamamen pasif donanımla (LM74610 ideal-diyot ORing modülleri) yapılır — bu sayede
// bir yazılım hatası asla modemin/Pi'nin gücünü kesemez. Firmware'in tek görevi:
// durumu izlemek, LED ile göstermek, ve Pi'ye USB-seri üzerinden raporlamak.
//
// Bağlantılar:
//   A0  -> AC-DC şarj adaptörünün DC çıkışı (24V), 10kohm(üst)+2.2kohm(alt) bölücüden sonra
//   A1  -> LiFePO4 batarya artı ucu, 10kohm(üst)+3.3kohm(alt) bölücüden sonra
//   D2  -> Durum LED'i (+ 220-330ohm direnç, GND'ye)
//   USB -> Pi'nin bir USB portuna (Pi tarafında /dev/ttyUSB0 veya /dev/ttyACM0 olarak görünür)
//
// KALİBRASYON (kurulumdan sonra MUTLAKA yapılmalı):
//   1. Multimetre ile AC-DC adaptörün gerçek çıkış voltajını ölçün (ör. 24.1V).
//   2. Seri port monitöründe basılan "VAC_RAW" değerini okuyun.
//   3. AC_DIVIDER_RATIO = gerçek_voltaj / VAC_RAW olacak şekilde güncelleyin.
//   4. Aynısını batarya için VBAT_RAW ve BAT_DIVIDER_RATIO ile tekrarlayın.
//   Direnç toleransı (%5-10) yüzünden hesaplanan teorik oranlar birebir tutmaz.

const int PIN_VAC = A0;
const int PIN_VBAT = A1;
const int PIN_LED = 2;

// Teorik başlangıç değerleri — kalibrasyon sonrası güncellenecek
float AC_DIVIDER_RATIO = (10000.0 + 2200.0) / 2200.0;   // ~5.545
float BAT_DIVIDER_RATIO = (10000.0 + 3300.0) / 3300.0;  // ~4.030
const float ADC_REF_VOLTAGE = 5.0;
const int ADC_MAX = 1023;

// Mains kaybı algılama eşiği: adaptör 24V civarı, ~10V altına düşerse "kayıp" kabul edilir
const float AC_LOST_THRESHOLD_V = 10.0;
// Debounce: durum değişikliğinin gürültü/dalgalanma olmadığından emin olmak için gereken süre
const unsigned long DEBOUNCE_MS = 2000;

// LiFePO4 4S (12.8V nominal) yaklaşık dinlenme-voltajı -> SOC tablosu (kaba tahmin)
// Not: LiFePO4 voltaj eğrisi çok düz olduğundan bu sadece kabaca bir gösterge, hassas değil.
struct SocPoint { float voltage; int soc; };
const SocPoint SOC_TABLE[] = {
  {14.20, 100}, {13.60, 95}, {13.30, 85}, {13.20, 75},
  {13.10, 60}, {13.00, 40}, {12.80, 20}, {12.50, 10},
  {12.00, 5},  {10.00, 0}
};
const int SOC_TABLE_SIZE = sizeof(SOC_TABLE) / sizeof(SOC_TABLE[0]);

const int SOC_LOW_THRESHOLD = 20;

enum UpsState { STATE_AC_OK, STATE_ON_BATTERY, STATE_ON_BATTERY_LOW };
UpsState currentState = STATE_AC_OK;

bool pendingAcLost = false;
unsigned long acLostSince = 0;
bool pendingAcRestored = false;
unsigned long acRestoredSince = 0;

unsigned long lastReportMs = 0;
const unsigned long REPORT_INTERVAL_MS = 5000;

unsigned long lastBlinkMs = 0;
bool ledOn = false;

float readVoltage(int pin, float dividerRatio) {
  int raw = analogRead(pin);
  float vAdc = (raw / (float)ADC_MAX) * ADC_REF_VOLTAGE;
  return vAdc * dividerRatio;
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

void updateLed() {
  unsigned long now = millis();
  unsigned long interval;

  switch (currentState) {
    case STATE_AC_OK:
      digitalWrite(PIN_LED, HIGH);
      return;
    case STATE_ON_BATTERY:
      interval = 500;
      break;
    case STATE_ON_BATTERY_LOW:
      interval = 150;
      break;
    default:
      interval = 1000;
  }

  if (now - lastBlinkMs >= interval) {
    lastBlinkMs = now;
    ledOn = !ledOn;
    digitalWrite(PIN_LED, ledOn ? HIGH : LOW);
  }
}

void setup() {
  pinMode(PIN_LED, OUTPUT);
  Serial.begin(9600);
  Serial.println("UPS_MONITOR_BOOT");
}

void loop() {
  float vAc = readVoltage(PIN_VAC, AC_DIVIDER_RATIO);
  float vBat = readVoltage(PIN_VBAT, BAT_DIVIDER_RATIO);
  int soc = estimateSoc(vBat);

  updateState(vAc, soc);
  updateLed();

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
    Serial.print(";UPTIME_S=");
    Serial.println(now / 1000);
  }
}
