# DC2DC UPS for Network — Modem + Pi Kesintisiz Güç Kaynağı

> Amaç: Keenetic Hero modemi ve Raspberry Pi CM5'i elektrik kesintilerinde en az 4 saat
> ayakta tutmak. 4 açık kaynak DC-UPS reposu (TobleMiner/DC-UPS, dilshan/12v-automatic-ups,
> issus/UninterruptableDCPowerSupply12V, 8bitmcu/mcuups) incelenip bunlardan esinlenen bir
> sentez mimari tasarlandı — birebir kopya değil.
>
> Bu proje, ev ağı dokümantasyonundan (`network-docs` reposu) ayrı, kendi başına bir donanım
> projesi olarak bu repoda tutuluyor. Ağ envanteriyle ilgili genel bağlam için `network-docs`
> reposundaki `CLAUDE.md` ve `NETWORK_INVENTORY.md` dosyalarına bakın.

## Durum (8 Eylül 2026)

- ✅ Faz 0 — Tüketim tahmini: Pi ~3-4W (ölçülmüş), modem 12V/2.5A (30W) adaptör tavanı worst-case kabul edildi.
- ✅ Faz 1 — BOM hazırlandı (aşağıda).
- ✅ Faz 2 (firmware kısmı) — `firmware/ups_monitor_esp32c3/ups_monitor_esp32c3.ino` yazıldı ve
  ESP32-C3'e yüklendi, Wi-Fi'ye bağlandı, test Telegram bildirimi başarıyla geldi (uçtan uca doğrulandı).
- ✅ Faz 5 (Telegram kısmı) önden alındı — firmware, ESP32'nin Wi-Fi'si üzerinden **Pi'den bağımsız**
  olarak doğrudan Telegram'a bildirim gönderiyor (Pi çökse/ağdan düşse bile bildirim gider).
- ✅ Akü satın alındı: **Power-Xtra PX26-12, 12V 26Ah VRLA kurşun asit** (LiFePO4 yerine — 8 Eylül 2026
  kararı, maliyet çok daha uygun). İlk şarjı yapıldı.
- ✅ **Bağlantı şeması hazır** → [DC-UPS Bağlantı Şeması](https://claude.ai/code/artifact/9c88a91e-7731-4aa1-8342-e20f013d8b7d)
  (gerilim bölücü ve LED bağlantı detaylarıyla, amatörlere hitap edecek şekilde genişletildi).
- ✅ **Gerilim bölücüler bağlandı ve kalibre edildi** (9 Eylül 2026) — multimetre referansı: adaptör
  24.0V, akü mains varken 14.0V / yokken 13.6V. `AC_DIVIDER_RATIO=9.83`, `BAT_DIVIDER_RATIO=5.07`
  (teorik 11.0/5.7'den belirgin sapma — direnç toleransı + ESP32 ADC doğrusalsızlığı nedeniyle beklenen).
- ✅ Telegram `/durum` komutu eklendi — anlık AC/batarya durumu sorgulanabiliyor.
- ⏳ Faz 3-4, 6 — Perfboard montajının geri kalanı (Schottky diyot, akü, çıkışlar), yük testi, kalıcı
  montaj, NETWORK_INVENTORY.md güncellemesi bekliyor.

## Mimari Özeti

> Tam görsel şema için bkz. [DC-UPS Bağlantı Şeması](https://claude.ai/code/artifact/9c88a91e-7731-4aa1-8342-e20f013d8b7d).

1. **Şarj katı:** XL4015 CC/CV buck şarj modülü, **13.6-13.8V float** çıkışına set edilir (akünün
   etiketindeki "Standby use" değeri — bkz. BOM #1 notu, ilk yoğun şarj için geçici olarak 14.4V kullanıldı).
2. **Mains-kaybı algılama:** ESP32-C3'ün ADC'si, AC-DC adaptörün DC çıkışını izler (24V → ~10V altı = kesinti).
3. **Kesintisiz geçiş — TEK diyot (D1):** Şarj modülü çıkışı ile ortak bara arasına **tek bir 1N5822
   Schottky diyot** (D1) konur — sadece bataryanın (mains kesintisinde) ölü şarj modülüne geri akım
   vermesini engellemek için. **Akü ile ortak bara arasında diyot YOK** — ikisi zaten aynı elektriksel
   düğüm (klasik float-şarj UPS topolojisi); bu yüzden kesinti anında hiçbir gecikme/anahtarlama olmadan
   akü devrede kalır. (İlk tasarımda LM74610 ideal-diyot modülleri, sonra 2x Schottky planlanmıştı;
   şemayı çizerken ikinci diyotun gereksiz olduğu — akü ve bara zaten aynı düğüm olduğu için — fark
   edildi ve tek diyota sadeleştirildi.)
4. **Çıkışlar:** Ortak bara → modem (barrel jack), 2x QCmini (12V→5V) → biri Pi CM5'e, biri ESP32-C3'e.
5. **Kontrolcü:** **ESP32-C3 Mini** (Hrant'ta zaten mevcut, Arduino Uno yerine tercih edildi — 8 Eylül
   2026 kararı). Batarya/mains durumunu izler, durum LED'i sürer, ve **Wi-Fi üzerinden doğrudan
   Telegram'a** bildirim gönderir — Pi'nin ayakta olmasına bağımlı değil (modem zaten bu UPS'ten
   beslendiği için Wi-Fi ağı kesinti sırasında da ayakta kalır). Bkz. "Firmware" bölümü — ESP32-C3'ün
   3.3V mantık seviyesi ve strapping pin kısıtları nedeniyle pin seçimi ve direnç bölücüler Arduino
   Nano/Uno'dan farklıdır.

**Kritik not — BMS yok:** LiFePO4'ün aksine seçilen kurşun asit akünün dahili koruma devresi (BMS) yok.
Firmware güç yolunu kesmediği için (bilinçli tasarım), çok uzun bir kesintide akü ~11.5V altına inip
zarar görebilir — Telegram'daki "DUSUK BATARYA" uyarısı geldiğinde manuel müdahale gerekebilir.

## Güç Bütçesi

| Değer | Miktar |
|---|---|
| Pi CM5 tüketimi (ölçülmüş) | ~3-4W |
| Modem adaptör tavanı (worst-case) | 30W (12V × 2.5A) |
| Toplam worst-case yük | ~34W (~2.83A @ 12V) |
| Hedef süre | ≥4 saat |
| Seçilen batarya | **Power-Xtra PX26-12, 12V 26Ah VRLA kurşun asit** (LiFePO4 yerine, maliyet nedeniyle — 8 Eylül 2026) |
| DoD hesabı | ~2.83A çekişte Peukert etkisiyle efektif ~22-23Ah; 4 saatlik hedef bu akımda ~11.3Ah çeker → **~%50 DoD** — kurşun asit için sağlıklı/önerilen aralık |
| Beklenen gerçek runtime | gerçek yük muhtemelen 34W tavanın altında olacağından muhtemelen 6-8+ saat |

## BOM (Sipariş Listesi)

| # | Parça | Spesifikasyon | Adet | TR arama terimi (Robotistan/Direnç.net/Trendyol vb.) |
|---|---|---|---|---|
| 1 | ~~LiFePO4 batarya paketi~~ Kurşun asit akü | **Power-Xtra PX26-12, 12V 26Ah VRLA** — dahili BMS YOK, satın alındı | 1 | ✅ Alındı |
| 2 | Şarj modülü | XL4015, giriş 24V, çıkış **13.6-13.8V float** (ilk yoğun şarjda geçici 14.4V), CC ~4-5A — satın alındı | 1 | ✅ Alındı |
| 3 | AC-DC adaptör (şarj için) | 24V DC, ≥5A — **Hrant'ta zaten mevcut** | 0 | ✅ Mevcut |
| 4 | Schottky diyot (D1, ORing için) | 1N5822 (3A/40V) — **tek adet yeterli**, satın alındı (5 adet alındı, 4'ü yedek) | 1 | ✅ Alındı |
| 5 | 12V→5V step-down (Pi CM5 + ESP32-C3 için, **2 adet, ayrı hatlar**) | **Kaplantis QCmini** — giriş 6-32V, çıkış varsayılan 5V (24W = ~4.8A'ya kadar, negotiation yapılmadığı için hep sabit 5V üretir), USB-A dişi çıkış. 85.40 TL/adet. 1'i Pi'yi, 1'i ESP32-C3'ü besler — aynı modülden iki bağımsız hat, WiFi TX darbeleri Pi'yi etkilemesin diye | 2 | Sipariş verildi (kaplantis.com, "USB DC step-down modül QCmini") |
| 5b | USB-A (erkek) → USB-C (erkek) kablo (Pi hattı için) | Modülün USB-A çıkışını Pi'nin USB-C girişine bağlamak için | 1 | **Zaten mevcut** (Baseus marka, kısa kablo) — satın almaya gerek yok |
| 6 | ~~Arduino Nano~~ ESP32-C3 Mini | **Zaten mevcut, satın almaya gerek yok** (Hrant'ta hazır) | 0 | — |
| 6b | ESP32-C3 besleme bağlantısı | İkinci QCmini modülünün 5V/GND çıkış pedleri, ESP32-C3 kartının 5V/GND pinine **doğrudan lehimlenir** (kalıcı montaj için kablo/konnektöre gerek yok) | — | — |
| 7 | Gerilim bölücü dirençler (ESP32-C3 için, 3.3V ADC tavanına göre) | AC-sense: 100kΩ+10kΩ, Bat-sense: 47kΩ+10kΩ | 1 set | "direnç seti 1/4W çeşitli değer" |
| 8 | 2 renkli (kırmızı/yeşil) durum LED'i, 3 bacaklı ortak katot | **Zaten mevcut** — GPIO6 (yeşil) ve GPIO7 (kırmızı), her ikisine 220-330Ω direnç, ortak bacak → GND | 0 | ✅ Mevcut |
| 9 | Sigortalar | 4A blade fuse + tutucu, 12V hat | 2 | "oto tipi bıçak sigorta 4A + yuva" |
| 10 | Terminal blokları | Vidalı, 2-3 pin | 6-8 | "vidalı terminal blok PCB 2 pin" |
| 11 | Perfboard | ~10x15cm | 1 | "delikli prototip PCB 10x15" |
| 12 | Proje kutusu | Havalandırmalı, ~150x100x50mm | 1 | "elektronik proje kutusu 150x100x50" |
| 13 | DC barrel jack (dişi, panel/kablo tipi) | **5.5mm dış / 2.1mm iç çap, orta uç (+)** — Keenetic Hero KN-1012 resmi adaptör spesifikasyonuyla doğrulandı (12V, 2.5A, 9-12V doğrultulmuş stabilize çıkış) | 2-3 | "DC jack 5.5x2.1mm dişi" |
| 14 | Kablo | 18AWG, esnek çok telli | birkaç metre | "18AWG silikon kablo" |

**Karar (#4, 8 Eylül 2026 — iki aşamalı):** Önce LM74610 ideal-diyot modülü (~44 USD/adet) maliyet
nedeniyle Schottky diyota geçildi. Sonra bağlantı şeması çizilirken **ikinci diyotun gereksiz olduğu**
anlaşıldı: akü ile ortak bara zaten aynı elektriksel düğüm (klasik float-şarj topolojisi), aralarında
diyota gerek yok — sadece şarj modülü ile bara arasına **tek bir D1 (1N5822)** yeterli, akünün ölü şarj
modülüne geri akım vermesini engellemek için. Sonuç: 1 diyot, ~0.3-0.5V düşüm, ~0.5-1W ısı kaybı,
pratikte runtime'a etkisi yok.

**Şarj voltajı (kurşun asit için, LiFePO4'ten farklı):** XL4015 çıkışını akünün etiketindeki
**"Standby use: 13.5-13.8V"** değerine (float, sürekli bağlı kullanım için) ayarlayın — bu hem D1'in
doğru çalışması için batarya aralığının üzerinde kalır hem de kurşun asidi aşırı şarjdan korur. İlk
yoğun şarj sırasında (akü belirgin boşken) "Cycle use: 14.4V" kullanıldı, doldu sayılınca (akım
~0.5A altına düşünce) float değerine geçilecek.

~~**Bekleyen doğrulama:** #13~~ ✅ çözüldü (8 Eylül 2026) — Keenetic resmi belgesinden doğrulandı: 5.5x2.1mm, merkez pozitif.
> Not: Resmi belge "9/12V" ve "3.0A'yı aşmayan" ifadesini kullanıyor — adaptör etiketi 12V/2.5A, tasarım
> zaten bu değere göre (worst-case 30W) boyutlandırılmıştı, değişiklik gerekmiyor.

**Karar (#6, 8 Eylül 2026):** Kontrolcü olarak Arduino Nano yerine Hrant'ta zaten bulunan **ESP32-C3
Mini** kullanılacak — Wi-Fi'si sayesinde Telegram bildirimini Pi'ye bağımlı olmadan doğrudan gönderebiliyor
(bkz. Mimari Özeti #5). Elde bir **Arduino Uno** da var ama kullanılmıyor (yedekte kalabilir). ESP32-C3
için ayrı bir 5V besleme hattı (#6b) öneriliyor — Pi'nin step-down modülüyle aynı hattı paylaşmak,
Wi-Fi verici darbelerinin (TX burst) yarattığı gerilim düşüşünün Pi'yi etkilemesi riskini taşır; ayrı
ve ucuz bir modülle bu risk sıfırlanıyor.

## Firmware

`firmware/ups_monitor_esp32c3/ups_monitor_esp32c3.ino` — ESP32-C3 Mini üzerinde çalışır
(Arduino IDE, board: "ESP32C3 Dev Module", Boards Manager'dan "esp32 by Espressif Systems" kurulu olmalı).

- **Güç yolunu kontrol etmez**, sadece izler; durum değişikliklerinde (mains kaybı/dönüşü, düşük batarya)
  **doğrudan Wi-Fi üzerinden Telegram'a** bildirim gönderir — Pi'ye bağımlı değil.
- **Telegram'dan sorgulanabilir:** bota `/durum` yazınca anlık AC hattı/batarya voltajı ve yüzdesini
  içeren bir yanıt döner (`getUpdates` ile kütüphanesiz polling, her 4 saniyede bir kontrol edilir).
  `/start` da kısa bir kullanım notu döner. Boot anında bekleyen eski komutlar sessizce temizlenir
  (yeniden başlatmada gecikmeli yanıt gitmesin diye).
- Ayrıca USB-seri üzerinden (115200 baud) durum satırı basar (`STATE=...;VAC=...;VBAT=...;SOC=...;WIFI=...`) — debug için.
- **Kurulum öncesi zorunlu adım:** `secrets.h.example` dosyasını aynı klasörde `secrets.h` olarak
  kopyalayın, kendi Wi-Fi ve Telegram bot bilgilerinizi girin. `secrets.h` `.gitignore`'da — **asla
  GitHub'a gitmez**, sırlarınız güvende kalır.
- **Kurulumdan sonra MUTLAKA** `AC_DIVIDER_RATIO` ve `BAT_DIVIDER_RATIO` sabitlerini multimetre ile
  kalibre edin (dosya içi yorumlarda adımlar var) — ESP32'nin ADC'si AVR'ye göre daha az doğrusal
  olduğundan bu adım burada daha da önemli.
- Pin seçimi bilinçli yapıldı: GPIO2/8/9 gibi boot-strapping pinlerinden kaçınıldı, ADC hatları
  GPIO0/GPIO1 (ADC1, Wi-Fi ile çakışmaz) üzerinden alındı — dosya başındaki yorumda gerekçesi var.
- **Durum LED'i 2 renkli (kırmızı/yeşil), 3 bacaklı, ortak katot:** GPIO6=yeşil (AC var=sabit yanık),
  GPIO7=kırmızı (batarya modu=yavaş yanıp söner, düşük batarya=hızlı yanıp söner). Ortak bacak GND'ye
  gider. **Kurulumdan önce doğrulayın:** multimetrenin diyot-test modunda siyah prob ortada, kırmızı
  prob dış bacakta iken LED yanmıyorsa LED'iniz ortak anottur — bu durumda ortak bacağı 3.3V'a bağlayıp
  koddaki `LED_ON`/`LED_OFF` sabitlerini ters çevirin (dosya başındaki yorumda detay var).
- SOC tablosu **kurşun asit** akü voltaj eğrisine göre kalibre edildi (12.7V=%100 … 11.5V=%0). Bu düğüm
  mains varken şarj modülü tarafından 13.6-13.8V'a sabitlendiğinden, SOC okuması yalnızca `ON_BATTERY`
  durumundayken (mains koptuğunda) anlamlıdır. Akünün BMS'i olmadığından `SOC_LOW_THRESHOLD` %25'e
  (LiFePO4'e göre daha erken) ayarlandı.

## Sonraki Adımlar

1. ~~BOM'daki parçaları sipariş et~~ ✅ akü, şarj modülü, diyotlar, dirençler, perfboard, barrel jack alındı.
2. ~~Firmware'i yükle, Wi-Fi/Telegram doğrula~~ ✅ tamamlandı (8 Eylül 2026) — test bildirimi geldi.
3. ~~Akünün ilk şarjı~~ ✅ yapılıyor (14.4V/4-5A ile, akım ~0.5A altına düşünce float'a (13.6-13.8V) geçilecek).
4. **[DC-UPS Bağlantı Şeması](https://claude.ai/code/artifact/9c88a91e-7731-4aa1-8342-e20f013d8b7d)'na göre perfboard üzerinde devreyi kur** — sırayı şemadaki "Bağlantı sırası" bölümü belirliyor.
5. Gerilim bölücüleri bağlayıp `AC_DIVIDER_RATIO`/`BAT_DIVIDER_RATIO` kalibrasyonunu multimetre ile yap.
6. Modem+Pi'yi devreye bağlayıp fiş çekme testiyle switchover'ı doğrula, gerçek runtime'ı ölç.
7. Kalıcı montaj (proje kutusu, sigortalama, etiketleme).
8. `network-docs` reposundaki `NETWORK_INVENTORY.md` ve `CLAUDE.md`'yi güncelle, bu repoya link ver.
