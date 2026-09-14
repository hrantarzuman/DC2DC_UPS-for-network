# XL4015 Kademeli (Akım Sınırlı) Başlatma Testi

## Neden gerekli

İki farklı XL4015 modülü, tek başına (bench güç kaynağıyla, yüksüz/dirençli yükle) test
edildiğinde sorunsuz çalıştı, ama **tam devreye (D1 diyot + akü + gerilim bölücüler) bağlanıp
AC-DC adaptörden doğrudan güç verildiği anda** duman çıkararak bozuldu.

Şüphelenilen neden: ucuz XL4015 kartlarının çoğunda düzgün bir soft-start yok. Çıkışta D1
üzerinden zaten **13.6-14V'ta duran, çok düşük iç dirençli bir akü** varken girişe aniden 24V
verilirse, geri besleme döngüsü daha oturmadan çıkış anlık olarak set değerinin üzerine
overshoot edebilir. Akü gibi neredeyse sıfır empedanslı bir yüke karşı bu overshoot, modülün
dahili anahtarlama elemanının kaldırabileceğinden çok daha yüksek akım çekilmesine ve anlık
arızaya yol açabilir.

Bu doküman, yeni bir XL4015 modülünü (veya tamir edilmiş/tekrar kullanılan bir modülü) tam
devreye bağlamadan önce izlenecek kademeli test prosedürünü tanımlar. Amaç: modül gerçekten
arızalıysa bunu **lab kaynağının akım limiti sayesinde zarar vermeden** yakalamak.

## Ön koşullar

- Ayarlanabilir, akım sınırlamalı (CC/CV) bir laboratuvar tipi güç kaynağı.
- Multimetre (voltaj ve — mümkünse ayrı bir ampermetre veya kaynağın üzerindeki akım göstergesi).
- Test edilecek XL4015 modülü, D1 (1N5822), akü ve gerilim bölücüler dahil **tam devre** kurulu
  olmalı (sadece AC-DC adaptör yerine lab kaynağı kullanılacak).

## Adımlar

1. **Trim pot doğrulama** — XL4015'i devreye bağlamadan önce, tek başına (akü/diyot bağlı
   değilken) lab kaynağıyla besleyip çıkışın **13.6-13.8V** (float) değerinde olduğunu teyit
   edin. Kart kutuya/perfboard'a monte edilirken pot kazayla oynamış olabilir.

2. **D1'in blokaj yönünü doğrulama** — XL4015'e henüz güç vermeden, sadece akü + D1'i bağlayın.
   XL4015'in çıkış ucunu (D1 anot tarafı) multimetreyle ölçün: **~0V civarı** okunmalı. Akü
   voltajına yakın bir değer (örn. 13V) görürseniz diyot ters bağlanmış veya sızıntı yapıyor
   demektir — devam etmeden önce bu düzeltilmeli.

3. **Kademeli/akım sınırlı başlatma** — Tam devre (D1 + akü + gerilim bölücüler) bağlıyken,
   AC-DC adaptör yerine lab kaynağını girişe bağlayın:
   - Kaynağın akım limitini **0.5-1A**'ya kısın, voltajı 24V'a ayarlayın, çıkışı açın.
   - Çıkış gerilimini (common bara/akü ucu) izleyin: düzgün şekilde 13.6-13.8V'a oturmalı,
     ani sıçrama/overshoot görülmemeli.
   - Kaynağın akım limitine takılıp takılmadığına (CC moduna geçip geçmediğine) bakın — takılırsa
     devrede anormal bir akım çekişi var demektir, ilerlemeyin.
   - Sorun yoksa akım limitini kademeli artırın: 1A → 2A → 3A → 4-5A, her adımda birkaç dakika
     bekleyip çıkış gerilimini ve modülün ısınmasını gözlemleyin.

4. **Gerçek adaptöre geçiş** — Kademeli test tamamen temiz geçtiyse (overshoot yok, CC moduna
   takılma yok, modül ısınmıyor), ancak o zaman lab kaynağını çıkarıp gerçek 24V/≥5A AC-DC
   adaptörü bağlayın.

## Ek koruma (donanım)

- AC adaptör çıkışı ile XL4015 VIN arasına **6-7A hızlı tip (fast-blow) sigorta** ekleyin —
  modül kısa devre olursa adaptörü/kabloyu korur (anlık dahili IC arızasını her zaman
  yakalayamaz, ama kademeli test asıl korumadır).
- D1 ile ortak bara arasına BOM'daki 4A sigortalardan birini koymak, arıza durumunda aküden
  geri kısa devre yolunu sınırlar.

## Notlar

- Gerilim bölücüler (AC-sense 100kΩ+10kΩ, Bat-sense 47kΩ+10kΩ) bu arızalarla ilgisiz bulundu —
  çektikleri akım (<0.3mA) ihmal edilebilir düzeyde, tasarım/bağlantı doğrulandı. Kök neden
  büyük olasılıkla akü zaten bağlıyken ani/sınırsız güç verilmesi.
- Bu test prosedürü, tamir edilmiş (IC değiştirilmiş) bir XL4015 kartı için de zorunludur —
  tamir başarılı görünse bile kart, aynı overshoot senaryosuna karşı yeni bir kart kadar
  savunmasızdır; kademeli testten geçmeden tam devreye güvenilmemeli.
