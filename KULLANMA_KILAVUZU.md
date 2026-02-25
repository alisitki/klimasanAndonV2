# Klimasan Idle Time Faz-2 Andon Pano — Kullanma Kılavuzu

**Versiyon:** 2.0  
**Tarih:** Şubat 2025

---

## 1. Genel Bakış

Andon Pano, üretim hattındaki çalışma, atıl ve planlı duruş sürelerini takip eden, hedef/gerçekleşen adet ve verim bilgilerini gösteren, LED cycle bar ile takt süresini izleyen bir endüstriyel görüntüleme ve izleme sistemidir.

### 1.1 Ekran Düzeni

Pano ekranı 4 sekmeden oluşur:

| Sekme | Bölüm | Hane | Açıklama |
|-------|--------|------|----------|
| **1. Sekme** | Saat | 6 hane (HH:MM:SS) | Güncel TR saati (otomatik) |
| | Duruş Süresi | 4 hane (MM:SS) | Mevcut duruş süresi |
| **2. Sekme** | Çalışma Zamanı | 6 hane (HH:MM:SS) | Toplam çalışma süresi |
| | Atıl Zaman | 6 hane (HH:MM:SS) | Toplam atıl süre |
| | Planlı Duruş | 6 hane (HH:MM:SS) | Toplam planlı duruş süresi |
| **3. Sekme** | Hedef Adet | 4 hane | IR kumandadan girilen hedef |
| | Gerçekleşen Adet | 4 hane | Turuncu buton ile artan adet |
| | Verim | 2 hane (%) | (Gerçekleşen / Hedef) × 100 |
| **4. Sekme** | Cycle Bar | LED strip | Takt süresi göstergesi (0-100%) |

---

## 2. Buton Kutusu

Cihaz üzerinde 4 adet fiziksel buton bulunur:

| Buton | Renk | Fonksiyon |
|-------|------|-----------|
| **Çalışma Zamanı** | 🟢 Yeşil | WORK moduna geçer, çalışma zamanı saymaya başlar |
| **Atıl Zaman** | 🔴 Kırmızı | IDLE moduna geçer, atıl zaman saymaya başlar |
| **Planlı Duruş** | 🟡 Sarı | PLANNED moduna geçer, planlı duruş saymaya başlar |
| **Adet** | 🟠 Turuncu | Gerçekleşen adedi 1 artırır + Cycle bar sıfırlar |

### 2.1 Çalışma Kuralları

- **Yeşil butona** basıldığında → Çalışma zamanı sayar, atıl/planlı durur.
- **Kırmızı butona** basıldığında → Atıl zaman sayar, çalışma/planlı durur. Duruş süresi saymaya başlar.
- **Sarı butona** basıldığında → Planlı duruş sayar, çalışma/atıl durur. Duruş süresi saymaya başlar.
- **Turuncu buton** yalnızca **WORK (Çalışma)** modunda çalışır. Atıl veya planlı duruş modlarında adet artmaz.

### 2.2 Duruş Süresi Mantığı

Kırmızı veya sarı butona basıldığında **Duruş Süresi** saymaya başlar. Yeşil butona tekrar basılana kadar sayar. Yeşil basıldığında duruş süresi donar ve ekranda görünmeye devam eder. Bir sonraki kırmızı/sarı basışta sıfırlanarak yeniden başlar.

---

## 3. IR Kumanda

### 3.1 Temel Fonksiyonlar

| Tuş | Fonksiyon | Açıklama |
|-----|-----------|----------|
| **ON/OFF** | Ekran Aç/Kapa | Ekranı açar veya kapatır. Açılırken tüm sayaçlar sıfırlanır, hedef adet korunur. |
| **RESET** | Ekran Reset | Tüm sayaçları sıfırlar (hedef adet korunur). |
| **MUTE** | Sıfırlama / Alarm Susturma | Alarm varsa susturur. Yoksa hedef adedi sıfırlar. |
| **VARDIYA** | Vardiya Durdur/Başlat | Ekranı dondurur, tüm sayaçlar durur. Tekrar basınca devam eder. |
| **OK** | Giriş Modundan Çık | Aktif giriş modunu kapatır. |

### 3.2 Mod Değiştirme (IR ile)

Kumanda üzerinde butonlara karşılık gelen renkli tuşlar bulunur:

| Tuş | Fonksiyon |
|-----|-----------|
| **Yeşil tuş** | WORK moduna geçer (fiziksel yeşil butonla aynı) |
| **Kırmızı tuş** | IDLE moduna geçer |
| **Sarı tuş** | PLANNED moduna geçer |
| **Mavi tuş** | Gerçekleşen adet +1 (sadece WORK modunda) |

### 3.3 Rakam Tuşları (0-9)

Rakam tuşları aktif giriş moduna göre farklı çalışır:

| Mod | Davranış |
|-----|----------|
| **Normal** | Hedef adet girişi (son 4 hane kayan pencere) |
| **Hedef Adet** | Hedef adet girişi |
| **Cycle Süresi** | Cycle bar hedef süresi girişi (saniye) |
| **Saat Ayarı** | Saat/dakika girişi |

---

## 4. Hedef Adet Ayarlama

### Hızlı Giriş (Doğrudan)
1. Kumanda üzerindeki rakam tuşlarına basarak hedef adedi girin.
2. Rakamlar **kayan pencere** mantığıyla çalışır: son 4 basamak ekranda görünür.
3. Örnek: `1`, `0`, `0`, `0` → Hedef: **1000**

### Sıfırlama
1. **MUTE** tuşuna basın → Hedef adet **0** olur.
2. Yeni rakam girmeye başlayın.

> **Not:** Hedef adet cihazın kalıcı belleğinde (NVS) saklanır. Cihaz kapatılıp açılsa bile hedef adet değişmez.

---

## 5. Saat Ayarı

DS1307 RTC modülü ile çalışır. Pil destekli olduğundan güç kesildiğinde saat korunur.

### Ayar Adımları

1. Kumandadan **Saat Ayarı** tuşuna basın.
   - Ekranda mevcut saat görünür, **saat haneleri** yanıp söner.
2. Rakam tuşlarıyla **saat** değerini girin (00-23).
   - Geçersiz değer girilirse (>23) eski değere döner.
3. **Saat Ayarı** tuşuna tekrar basın (veya **OK**).
   - **Dakika haneleri** yanıp söner.
4. Rakam tuşlarıyla **dakika** değerini girin (00-59).
   - Geçersiz değer girilirse (>59) eski değere döner.
5. **Saat Ayarı** tuşuna tekrar basın (veya **OK**).
   - Saat kaydedilir, saniyeler **00**'a sıfırlanır.

### Örnek

Saat 14:30 yapmak için:

| Adım | İşlem | Ekran |
|------|-------|-------|
| 1 | Saat Ayarı tuşu | Saat haneleri yanıp söner |
| 2 | `1` bas, `4` bas | Saat: **14** |
| 3 | Saat Ayarı tuşu | Dakika haneleri yanıp söner |
| 4 | `3` bas, `0` bas | Dakika: **30** |
| 5 | Saat Ayarı tuşu | ✅ 14:30:00 kaydedildi |

---

## 6. Cycle Bar (LED Strip) Ayarları

Cycle bar, her üretim döngüsünün süresini görsel olarak takip eder.

### 6.1 Çalışma Mantığı

- Turuncu butona (Adet) basıldığında cycle bar **sıfırdan** dolmaya başlar.
- Hedef cycle süresine göre bar ilerler:

| Aralık | Renk | Anlamı |
|--------|------|--------|
| %0 – %70 | 🟢 Yeşil | Normal tempo |
| %70 – %90 | 🟠 Turuncu | Dikkat — hedef süreye yaklaşıyor |
| %90 – %100 | 🔴 Kırmızı | Uyarı — süre dolmak üzere |
| **> %100** | 🔴 Kırmızı + 🔊 Buzzer | **ALARM** — süre aşıldı |

### 6.2 Alarm Durumu

Cycle süresi aşıldığında:
- LED bar **kırmızı yanıp söner**
- **Buzzer sesli alarm** verir
- Alarm **susturmak için** kumandadan **MUTE** tuşuna basılmalıdır
- MUTE'a basıldıktan sonra: buzzer susar, bar **kırmızı kalır**
- Bir sonraki turuncu butona kadar kırmızı durumda kalır

### 6.3 Cycle Süresi Ayarlama

1. Kumandadan **Cycle Süresi** tuşuna basın.
2. Mevcut değeri sıfırlamak isterseniz **MUTE** tuşuna basın.
3. Rakam tuşlarıyla yeni cycle süresini girin (saniye cinsinden).
4. **OK** tuşuna basarak giriş modundan çıkın.

### Örnek

Cycle süresini 90 saniye yapmak için:

| Adım | İşlem | Değer |
|------|-------|-------|
| 1 | Cycle Süresi tuşu | Giriş modu aktif |
| 2 | MUTE bas | Süre: **0** |
| 3 | `9` bas | Süre: **9** |
| 4 | `0` bas | Süre: **90** |
| 5 | OK bas | ✅ Kaydedildi |

> **Not:** Cycle süresi kalıcı bellekte saklanır. Cihaz kapatılıp açılsa bile korunur.

---

## 7. LED Menü (Parlaklık & Süre Ayarı)

LED strip'in parlaklık ve süre ayarları menü sistemi üzerinden yapılır.

### Menüye Giriş ve Navigasyon

| Basış | Adım | Açıklama |
|-------|------|----------|
| **1. MENU** | Parlaklık Ayarı | LED'ler yanar. Yukarı/Aşağı tuşlarıyla parlaklık (1-4) ayarlanır. |
| **2. MENU** | Süre Ayarı | Rakam tuşlarıyla cycle süresi girilir. MUTE ile sıfırlanır. |
| **3. MENU** | Kaydet & Çık | Ayarlar kalıcı belleğe kaydedilir. |

### Parlaklık Kademeleri

| Seviye | Parlaklık |
|--------|-----------|
| 1 | %5 (Çok düşük) |
| 2 | %15 (Düşük) |
| 3 | %35 (Orta — Varsayılan) |
| 4 | %65 (Yüksek) |

---

## 8. Vardiya Yönetimi

### Vardiya Durdurma
1. Kumandadan **Vardiya** tuşuna basın.
2. Ekrandaki tüm değerler **donar** — hiçbir sayaç ilerlemez.
3. Operatör ekrandaki değerleri okuyarak **Vardiya Devir Formu**'na yazar.

### Vardiya Başlatma
1. **Vardiya** tuşuna tekrar basın.
2. Sayaçlar kaldığı yerden devam eder.

### Vardiya Başı (Yeni Vardiya)
1. **RESET** tuşuna basın → Tüm sayaçlar sıfırlanır.
2. **Yeşil butona** basın → Çalışma zamanı saymaya başlar.

---

## 9. Güç Kesintisi ve Kurtarma

Cihaz, mevcut durumu kalıcı belleğe (NVS) periyodik olarak kaydeder. Güç kesilip geri geldiğinde:

| Senaryo | Davranış |
|---------|----------|
| Vardiya çalışıyorken kapandı | Kaydedilen modda sayaçlar kaldığı yerden devam eder. Kapalı kalınan süre ilgili sayaca eklenir (max 24 saat). |
| Vardiya durdurulmuşken kapandı | Ekran donuk olarak açılır, hiçbir sayaç saymaz. Vardiya tuşuyla devam edilir. |
| İlk kez açılış / NVS boş | Tüm sayaçlar sıfır, bekleme modunda başlar. |

> **Önemli:** Hedef adet, cycle süresi ve LED parlaklığı her durumda korunur.

---

## 10. Hızlı Başvuru — Kumanda Tuşları Özeti

| Tuş | Fonksiyon |
|-----|-----------|
| **ON/OFF** | Ekran aç/kapa (açılırken tam reset) |
| **RESET** | Sayaçları sıfırla |
| **MUTE** | Alarm sustur / Hedef adet sıfırla |
| **VARDIYA** | Vardiya durdur/başlat |
| **MENU** | LED ayar menüsü (Parlaklık → Süre → Kaydet) |
| **YUKARI (▲)** | Parlaklık artır (menüdeyken) |
| **AŞAĞI (▼)** | Parlaklık azalt (menüdeyken) |
| **SAAT AYARI** | Saat ayarlama modu |
| **HEDEF ADET** | Hedef adet giriş modu |
| **CYCLE SÜRESİ** | Cycle süresi giriş modu |
| **OK** | Giriş modundan çık |
| **0-9** | Rakam girişi (aktif moda göre) |
| **Yeşil** | WORK modu |
| **Kırmızı** | IDLE modu |
| **Sarı** | PLANNED modu |
| **Mavi** | Adet +1 (sadece WORK) |
