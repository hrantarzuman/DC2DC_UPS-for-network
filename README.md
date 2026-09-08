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
- ✅ Faz 2 (firmware kısmı) — `firmware/ups_monitor/ups_monitor.ino` yazıldı. Donanım montajı parçalar
  gelince yapılacak.
- ⏳ Faz 3-6 — Yük testi, kalıcı montaj, Telegram entegrasyonu, NETWORK_INVENTORY.md güncellemesi bekliyor.

## Mimari Özeti

1. **Şarj katı:** Ayarlanabilir CC/CV buck şarj modülü, LiFePO4 için 14.6V absorption'a set edilecek.
2. **Mains-kaybı algılama:** Arduino ADC, AC-DC adaptörün DC çıkışını izler (24V → ~10V altı = kesinti).
3. **Kesintisiz geçiş:** 2x Schottky diyot ORing (mains yolu + batarya yolu → ortak 12V bara, hangi
   kaynak voltajı yüksekse o besler). Röle yok, gecikme/sekme riski yok — **firmware güç yolunu kontrol
   etmez, sadece izler.** (İlk tasarımda LM74610 ideal-diyot modülü planlanmıştı; ~44 USD/adet maliyeti
   nedeniyle 8 Eylül 2026'da basit Schottky diyota geçildi — bkz. BOM #4 notu.)
4. **Çıkışlar:** 12V bara → modem (barrel jack), 12V→5V/3A step-down (USB-C) → Pi CM5.
5. **Kontrolcü:** Arduino Nano, batarya/mains durumunu USB-seri üzerinden Pi'ye raporlar (durum LED'i de var).

## Güç Bütçesi

| Değer | Miktar |
|---|---|
| Pi CM5 tüketimi (ölçülmüş) | ~3-4W |
| Modem adaptör tavanı (worst-case) | 30W (12V × 2.5A) |
| Toplam worst-case yük | ~34W |
| Hedef süre | ≥4 saat |
| Gerekli kullanılabilir kapasite | ~170Wh minimum |
| Seçilen batarya | 12.8V (4S) LiFePO4, 20Ah (~256Wh nominal) — güvenlik marjı için |
| Beklenen gerçek runtime | muhtemelen 6-8+ saat (gerçek yük tavan değerin altında olacaktır) |

## BOM (Sipariş Listesi)

| # | Parça | Spesifikasyon | Adet | TR arama terimi (Robotistan/Direnç.net/Trendyol vb.) |
|---|---|---|---|---|
| 1 | LiFePO4 batarya paketi | 12.8V (4S), 20Ah, dahili BMS'li | 1 | "12.8V 20Ah LiFePO4 batarya BMS'li" |
| 2 | LiFePO4 CC/CV şarj modülü | Giriş 15-24V, çıkış 14.6V'a ayarlanabilir, 3-5A | 1 | "XL4015 step down modül ayarlanabilir 5A" veya "LiFePO4 şarj modülü 14.6V" |
| 3 | AC-DC adaptör (şarj için) | 24V DC, ≥5A | 1 | "24V 5A adaptör" |
| 4 | Schottky diyot (ORing için) | TO-220 paket, ≥5A, ≥40V (ör. SB560 veya 1N5822) | 2 | "SB560 schottky diyot TO-220" veya "1N5822 diyot" |
| 5 | 12V→5V/3A step-down (USB-C çıkışlı) | Giriş 9-15V, çıkış 5V/3A sabit | 1 | "DC-DC step down USB-C çıkışlı 5V 3A modül" |
| 6 | Arduino Nano | ATmega328, USB programlanabilir | 1 | "Arduino Nano V3 CH340" |
| 7 | Gerilim bölücü dirençler | AC-sense: 10kΩ+2.2kΩ, Bat-sense: 10kΩ+3.3kΩ | 1 set | "direnç seti 1/4W çeşitli değer" |
| 8 | Durum LED'i + 220-330Ω direnç | Mains/batarya göstergesi | 1-2 | "5mm LED kırmızı yeşil" |
| 9 | Sigortalar | 4A blade fuse + tutucu, 12V hat | 2 | "oto tipi bıçak sigorta 4A + yuva" |
| 10 | Terminal blokları | Vidalı, 2-3 pin | 6-8 | "vidalı terminal blok PCB 2 pin" |
| 11 | Perfboard | ~10x15cm | 1 | "delikli prototip PCB 10x15" |
| 12 | Proje kutusu | Havalandırmalı, ~150x100x50mm | 1 | "elektronik proje kutusu 150x100x50" |
| 13 | DC barrel jack (dişi, panel/kablo tipi) | **5.5mm dış / 2.1mm iç çap, orta uç (+)** — Keenetic Hero KN-1012 resmi adaptör spesifikasyonuyla doğrulandı (12V, 2.5A, 9-12V doğrultulmuş stabilize çıkış) | 2-3 | "DC jack 5.5x2.1mm dişi" |
| 14 | Kablo | 18AWG, esnek çok telli | birkaç metre | "18AWG silikon kablo" |

**Karar (#4, 8 Eylül 2026):** LM74610 ideal-diyot modülü ~44 USD/adet çıktığından vazgeçildi, basit
Schottky diyot ORing'e geçildi. Fark: ideal diyota göre ~0.3-0.5V gerilim düşümü ve diyot başına
~1-1.5W ısı kaybı olur (bu akım seviyesinde küçük bir klips-tipi soğutucu yeterli) — pratikte runtime'ı
ölçülemeyecek kadar az etkiler, TR'de her yerde bulunur, lehimlemesi kolay (TO-220, büyük bacaklı).
**Önemli tasarım notu:** Schottky ORing'in doğru çalışması için şarj modülünün DC bara çıkış voltajı
(mains varken), bataryanın olası tüm SOC aralığındaki dinlenme voltajından **her zaman yüksek**
olmalı — aksi halde mains varken bile batarya boşalmaya devam edebilir. Şarj modülünü ~14.0-14.2V
sabit çıkışa ayarlayın (LiFePO4 4S için hem güvenli hem batarya aralığının üstünde).

~~**Bekleyen doğrulama:** #13~~ ✅ çözüldü (8 Eylül 2026) — Keenetic resmi belgesinden doğrulandı: 5.5x2.1mm, merkez pozitif.
> Not: Resmi belge "9/12V" ve "3.0A'yı aşmayan" ifadesini kullanıyor — adaptör etiketi 12V/2.5A, tasarım
> zaten bu değere göre (worst-case 30W) boyutlandırılmıştı, değişiklik gerekmiyor.

## Firmware

`firmware/ups_monitor/ups_monitor.ino` — Arduino Nano üzerinde çalışır. Önemli:
- **Güç yolunu kontrol etmez**, sadece izler ve Pi'ye USB-seri üzerinden raporlar (`STATE=...;VAC=...;VBAT=...;SOC=...`).
- Kurulumdan sonra `AC_DIVIDER_RATIO` ve `BAT_DIVIDER_RATIO` sabitleri multimetre ile kalibre edilmeli (dosya içi yorumlarda adımlar var).
- LiFePO4 SOC tahmini kaba bir tablo ile yapılır (LiFePO4 voltaj eğrisi düz olduğundan hassas değildir, sadece düşük-batarya uyarısı için yeterli).

## Sonraki Adımlar

1. BOM'daki parçaları sipariş et (barrel jack çapını önce ölç).
2. Parçalar gelince perfboard üzerinde masaüstü prototip kur.
3. Firmware'i Arduino IDE ile yükle, `AC_DIVIDER_RATIO`/`BAT_DIVIDER_RATIO` kalibrasyonunu yap.
4. Modem+Pi'yi prototipe bağlayıp fiş çekme testiyle switchover'ı doğrula, gerçek runtime'ı ölç.
5. Kalıcı montaj + Telegram WoL bot'a düşük-batarya/mains-kaybı bildirimi ekle.
6. `NETWORK_INVENTORY.md` ve `CLAUDE.md`'yi güncelle.
