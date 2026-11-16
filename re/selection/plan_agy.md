# AoK HD 5.8 — Maksimum Birim Seçim Limiti (Selection Cap) ve 150'ye Genişletme Planı

> **Hedef:** Age of Empires II HD 5.8 (`AoK HD.exe`, 32-bit PE, ImageBase `0x400000`) üzerinde maksimum birim seçim limitini (AoK'ta 40, HD'de 60) incelemek, bellek yerleşimini ve limit sabitlerini tam adres ve opcode seviyesinde belgelemek, ve Wololoader DLL üzerinden limiti 150'ye çıkaracak somut bir plan hazırlamak.
> **Durum:** Salt-okunur araştırma tamamlandı. Tüm adresler ve baytlar `AoK HD.exe` disassembly ve `re/out/decompiled.c` üzerinden doğrulanmıştır (**VERIFIED**).

---

## 1. Yönetici Özeti ve Mimari Genel Bakış

Age of Empires II HD 5.8'de birim seçim sistemi tek bir merkezde değil, birbirine bağlı 4 alt sistemde 60 (`0x3c`) sabitine bağımlıdır:
1. **Oyuncu Nesnesi (`WorldPlayer`):** `player + 0x160` ofsetinde başlayan 60 elemanlık sabit `RGE_Static_Object*` dizisi (240 bayt) ve `player + 0x254` ofsetindeki sayaç (`DWORD`).
2. **Kutuyla Seçim (Drag-Box) & Ekran Sorgusu:** `0xde0430` adresindeki statik global arabellek (her biri 12 bayt olan 60 birimlik kayıt = 720 bayt).
3. **Komut Oluşturma ve Ağ Protokolü (`command.cpp` / `TRIBE_Command`):** Seçilen birimlerin ID'lerini komut paketine yazan `0xaf4e40` arabelleği (60 DWORD), paket çalıştırma arabelleği `0xafb510` (60 DWORD) ve `TRIBE_Command` içindeki oyuncu başına son komut geçmişi dizisi `command + 0x12c` (`9 * 60 * 4 = 2160` bayt).
4. **Kullanıcı Arayüzü Paneli (`tpnl_obj`):** 60 ikonu alt panelde gruplayıp çizen kod (`0x716360`).

---

## 2. Bellek Yerleşimi ve Veri Yapıları (VERIFIED)

### 2.1. `WorldPlayer` Seçim Depolama Alanı
`WorldPlayer` nesnesi `RGE_Player` türevidir. Seçim verileri doğrudan nesnenin içinde sabit dizi olarak tutulur:

| Ofset | Tip | İsim / Açıklama | Doğrulama Durumu |
|---|---|---|---|
| `this + 0x15c` | `RGE_Static_Object*` | `selected_unit_primary` (Seçilen birincil birim pointer'ı) | **VERIFIED** (`0x754aa3`, `0x755490`) |
| `this + 0x160` | `RGE_Static_Object*[60]` | `selected_units[60]` (60 elemanlık pointer dizisi, `0x160`..`0x24f`) | **VERIFIED** (`0x754b2a`, `0x7554a9`) |
| `this + 0x250` | `DWORD` | İç durum / hizalama alanı | **VERIFIED** |
| `this + 0x254` | `int` | `selected_unit_count` (Seçili birim sayısı: 0 .. 60) | **VERIFIED** (`0x754b1d`, `0x755476`) |

```
WorldPlayer (this):
+0x15C: [ Primary Unit Pointer (4B) ]
+0x160: [ Unit 0 ] [ Unit 1 ] ... [ Unit 59 ]  (240 bytes = 0xF0)
+0x250: [ Dword pad/state (4B) ]
+0x254: [ Selected Count (4B, 0..60) ]
+0x258: [ Takip eden diğer player struct alanları... ]
```

> [!CAUTION]
> `player + 0x160` dizisinin hemen ardından `0x254` (count) ve `0x258+` diğer kritik oyuncu alanları gelir. Bu diziyi yerinde 150 elemana (600 bayt) genişletmek bellek taşmasına (struct overwrite) ve anında çökmeye yol açar. Bu alan harici bir dinamik diziye yönlendirilmelidir (relocation).

### 2.2. Birim Bayrakları (`RGE_Static_Object`)
Her birim kendi seçim ve grup durumunu kendi içinde taşır:
- `unit + 0x3a` (`byte`):
  - `Bit 0` (`& 0x01`): `SELECTED` bayrağı (`WorldPlayer::select_unit` tarafından `|= 1` yapılır, `unselect` ile `&= 0xfe` yapılır).
  - `Bit 3` (`& 0x08`): `IN_GROUP` bayrağı.
- `unit + 0x158` (`DWORD`): **Kontrol Grubu Bitmaskesi** (Aşağıda detaylandırılmıştır).

### 2.3. Statik Global Arabellekler
Oyun motoru seçim işlemleri ve komut hazırlığı için statik veri segmentinde (.data / .bss) 60 birimlik sabit arabellekler kullanır:

| Sembol / Adres | Boyut | Eleman Yapısı | Kullanıldığı Yer | Durum |
|---|---|---|---|---|
| `DAT_00de0430` | 720 bayt (`60 * 12`) | `{ int unit_id; int x; int y; }` | Sürükleyerek (drag-box) seçim arabelleği (`0x87cee0`) | **VERIFIED** |
| `DAT_00af4e40` | 240 bayt (`60 * 4`) | `int unit_id[60]` | Oyuncu komutu gönderim öncesi ID listesi (`0x74ef20`) | **VERIFIED** |
| `DAT_00afb510` | 240 bayt (`60 * 4`) | `int unit_id[60]` | Komut paketi ayrıştırıcı işleme listesi (`0x8095b0`) | **VERIFIED** |
| `DAT_00afb50c` | 4 bayt | `int count` | `DAT_00afb510` içindeki birim adedi | **VERIFIED** |

### 2.4. Komut ve Ağ Yapısı (`TRIBE_Command` / `RGE_Command`)
Komut nesnesi (`this` = `g_command` veya `TRIBE_Command*`), oyunculara verilen emirleri depolar ve ağ üzerinden iletir:

| Ofset | Boyut | Açıklama | Durum |
|---|---|---|---|
| `command + 0x30` | 240 bayt | `last_sent_units[60]`: Yerel oyuncunun son komutta gönderdiği 60 birim ID'si | **VERIFIED** (`0x8095b0`) |
| `command + 0x120 + player` | 9 bayt | `last_commanded_count[9]`: Her oyuncunun (0..8) son komutundaki birim sayısı | **VERIFIED** (`0x80e6c0`) |
| `command + 0x12c + player*0xf0` | 2160 bayt | `last_commanded_units[9][60]`: Her oyuncu için 60 DWORD'lük son emir verilen birim önbelleği | **VERIFIED** (`0x80dad0`, `0x80e6c0`) |

---

## 3. Doğrulanmış Limit Sabitleri (`0x3c` = 60) ve Kod Konumları

Tüm adresler `AoK HD.exe` disassembly çıktısıyla teyit edilmiştir.

### 3.1. Temel Seçim ve Seçimden Çıkarma Fonksiyonları

| Adres | Orijinal Baytlar | Assembly (Intel) | Fonksiyon & İşlev | Durum |
|---|---|---|---|---|
| `0x754b23` | `83 f8 3c` | `cmp eax, 0x3c` | `WorldPlayer::select_unit`: Seçili sayısı >= 60 ise seçimi engeller | **VERIFIED** |
| `0x754bb0` | `83 be 54 02 00 00 3c` | `cmp dword ptr [esi+0x254], 0x3c` | `WorldPlayer::select_unit`: İkincil sınır kontrolü | **VERIFIED** |
| `0x755722` | `83 fa 3c` | `cmp edx, 0x3c` | `WorldPlayer::unselect_all`: 60 birimlik diziyi sıfırlama döngüsü sınırı | **VERIFIED** |
| `0x755797` | `83 f9 3b` | `cmp ecx, 0x3b` | `WorldPlayer::unselect_object`: Dizi kaydırma (shift) döngüsü (`0x3b = 59`) | **VERIFIED** |
| `0x7554a0` | - | - | `WorldPlayer::unselect_at_index`: Dizideki indisi çıkartıp arkasındakileri kaydırır | **VERIFIED** |

### 3.2. Sürükleyerek Kutu Seçimi (Drag-Box Selection)

| Adres | Orijinal Baytlar | Assembly (Intel) | Fonksiyon & İşlev | Durum |
|---|---|---|---|---|
| `0x87d07d` | `81 ff 04 07 de 00` | `cmp edi, 0xde0704` | `FUN_0087cee0`: `0xde0430` tampon sonu kontrolü (`0xde0434 + 60*12 = 0xde0704`) | **VERIFIED** |
| `0x87d4fb` | `81 fa 04 07 de 00` | `cmp edx, 0xde0704` | `FUN_0087d110`: Aday birimleri filtreleme tampon sonu kontrolü | **VERIFIED** |
| `0x87d57c` | `b8 3c 00 00 00` | `mov eax, 0x3c` | `FUN_0087d110`: Maksimum eklenecek birim tavanı (60) | **VERIFIED** |
| `0x86f8a4` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_0086f880`: Görünüm seçim sonuç kısıtlaması | **VERIFIED** |
| `0x822261` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_00822230`: Ekran alanı birim sorgu limiti | **VERIFIED** |

### 3.3. Shift-Tıklama ve Çift Tıklama (Select-All-of-Type)

| Adres | Orijinal Baytlar | Assembly (Intel) | Fonksiyon & İşlev | Durum |
|---|---|---|---|---|
| `0x887747` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_00887660`: Shift-seçim stack tamponu üst sınırı (`jl ... mov eax, 0x3b`) | **VERIFIED** |
| `0x86f3f0` | - | - | `FUN_0086f3f0`: Çift tıklama ile ekrandaki aynı tip birimleri seçme. Doğrudan `WorldPlayer::select_unit` (`0x754a20`) çağırır; sınır `0x754b23`'te denetlenir. | **VERIFIED** |
| `0x886970` | - | - | `on_left_click`: Shift tuşuna göre seçim ekleme/çıkarma yönlendirmesi | **VERIFIED** |

### 3.4. Boşta Köylü (Idle Villager) ve Tüm Askerleri Seçme Hotkey'leri
- Boşta köylü (`.` hotkey'i) tek tek birimleri döngüyle seçtiği için doğrudan `select_unit` çağırır (tekil seçimde sınır 1'dir).
- Tüm Boşta Köylüleri Seç / Tüm Askerleri Seç hotkey'leri: Oyuncu birimleri arasında döngü kurup `WorldPlayer::select_unit(unit, 0)` çağırır. Seçim sınırı `0x754b23` tarafından 60'ta kesilir.

### 3.5. Kullanıcı Arayüzü Seçim Paneli (`tpnl_obj::draw_group`)

| Adres | Orijinal Baytlar | Assembly (Intel) | Fonksiyon & İşlev | Durum |
|---|---|---|---|---|
| `0x71645f` | `83 f9 3c` | `cmp ecx, 0x3c` | `tpnl_obj::draw_group`: 60'tan fazla birim ikonunu çizmeyi atlar | **VERIFIED** |
| `0x716534` | `b8 4c 02 00 00` | `mov eax, 0x24c` | `0x160 + 59*4 = 0x24c`: Son birim pointer ofseti | **VERIFIED** |
| `0x716746` | `3d 60 01 00 00` | `cmp eax, 0x160` | Dizi başlangıç kontrolü (geriye doğru döngü) | **VERIFIED** |

### 3.6. Emir Gönderimi ve Komut Dağıtım Kodları (`command.cpp`)

| Adres | Orijinal Baytlar | Assembly (Intel) | Fonksiyon & İşlev | Durum |
|---|---|---|---|---|
| `0x74f085` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_0074ef20`: Seçili birimlerin ID'lerini `0xaf4e40`'a kopyalama döngüsü | **VERIFIED** |
| `0x80e6c8` | `b9 3c 00 00 00` | `mov ecx, 0x3c` | `FUN_0080e6c0`: `last_commanded_units` önbellek temizleme (`rep stosd`) | **VERIFIED** |
| `0x80a49c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Sağ tık / Hedefe hareket komutu birim ekleme kontrolü | **VERIFIED** |
| `0x80a99c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Saldırı komutu (`do_attack`) birim sınırı | **VERIFIED** |
| `0x80b1ac` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | İş / Toplama / Tamir komutu birim sınırı | **VERIFIED** |
| `0x80b74c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Devriye (patrol) komutu birim sınırı | **VERIFIED** |
| `0x80ba4c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Koruma (guard) komutu birim sınırı | **VERIFIED** |
| `0x80bd1c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Takip (follow) komutu birim sınırı | **VERIFIED** |
| `0x80c69c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Keşif / Hedefe hücum komutu birim sınırı | **VERIFIED** |
| `0x80cd4c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Formasyon değiştirme komutu birim sınırı | **VERIFIED** |
| `0x80d71c` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Garnizona girme komutu birim sınırı | **VERIFIED** |
| `0x814fdc` | `83 3d 0c b5 af 00 3c` | `cmp dword ptr [0xafb50c], 0x3c` | Satış/bırakma komutu birim sınırı | **VERIFIED** |

### 3.7. Formasyon ve Grup Birleştirme Sınırı (`baseworld.cpp`)

| Adres | Orijinal Baytlar | Assembly (Intel) | Fonksiyon & İşlev | Durum |
|---|---|---|---|---|
| `0x739ca7` | `83 f8 3c` | `cmp eax, 0x3c` | `FUN_00739b30`: Grup birleştirme döngü güvenliği (`jg 0x739d93` -> sonsuz döngü uyarısı) | **VERIFIED** |

---

## 4. Kontrol Grupları (Control Groups 1..9) Mimarisi (VERIFIED)

Kontrol grupları oyuncu nesnesinde birim listeleri olarak **saklanmaz**.

1. **Birim İçi Depolama:** Her birimin `unit + 0x158` ofsetinde 32-bit bir DWORD bitmaske bulunur.
   - Grup 1: `bit 0` (`0x00000001`)
   - Grup 2: `bit 1` (`0x00000002`)
   - ...
   - Grup 9: `bit 8` (`0x00000100`)
2. **Grup Tanımlama (`Ctrl + 1..9` -> `FUN_0074f0c0`):**
   - Fonksiyon `1 << (grup - 1)` maskesini hesaplar.
   - Önce oyuncunun tüm birimlerini tarayarak bu grubun eski üyelerinden bu biti temizler (`unit + 0x158 &= ~mask`).
   - Ardından seçili birimler üzerinde döner:
     - Adres `0x74f07e`: `while (i < 0x3c)` sınırıyla `player->selected_units[i]` dizisini okur.
     - Her birimin `unit + 0x158` alanına `|= mask` uygular.
3. **Grubu Çağırma (`1..9` tuşu -> `FUN_007547f0`):**
   - Oyuncunun tüm aktif birimlerini tarar.
   - `if (unit->0x158 & mask)` şartını sağlayan her birim için `WorldPlayer::select_unit(unit, 0)` çağırır.
   - Bu çağrı da yine `0x754b23`'teki 60 sınırına tabidir.

---

## 5. Kayıtlı Oyunlar (Save Games) ve Kayıt Dosyaları (Replays) (VERIFIED)

### 5.1. Oyun Kaydı (`.aoe2spgame` / `.msx`)
- `WorldPlayer::selected_units` (`0x160`..`0x254`) **savegame dosyasına kaydedilmez**. Geçici (volatile/UI) ekran durumudur.
- Ancak `unit + 0x158` (kontrol grubu bitmaskesi), birim nesnesinin kalıcı durumu olduğu için save dosyasına yazılır ve geri yüklenir.

### 5.2. Tekrar Kayıtları (Recorded Games / `.mgx`)
- Motor her karede seçili birim dizisini kaydetmez.
- Bir emir verildiğinde (`FUN_0080ef70`), komut paketine şu başlık yazılır:
  - Byte 0..3: Komut tipi ve oyuncu ID.
  - Byte 4: `unit_count` (1 bayt).
  - Byte 5..7: Hedef / koordinat verisi.
  - Takip eden baytlar: `unit_count * 4` baytlık `unit_id` dizisi.
- **Optimizasyon (0xFF bayrağı):** Eğer verilen emir az önce emir verilen aynı seçim grubuna gidiyorsa, `unit_count = 0xff` yazılır ve birim ID listesi tekrar pakete eklenmez. Alıcı taraf (`FUN_0080dad0`) `0xff` gördüğünde `last_commanded_units[player]` önbelleğindeki listeyi tekrar kullanır.

---

## 6. Limiti 150'ye Çıkarma Uygulama Planı (Implementation Plan)

Limiti 60'tan 150'ye (`0x96`) çıkarmak için izlenecek 6 adımlı kesin strateji:

### Adım 1: `WorldPlayer` Seçim Dizisinin Yönlendirilmesi (Relocation)
`player + 0x160` alanı 240 bayttır; 150 birim için 600 bayt gerekir.
- **Çözüm:** Wololoader DLL içinde `RGE_Static_Object* g_custom_selection[9][150]` (her oyuncu için 600 baytlık statik dizi) tahsis edilir.
- `WorldPlayer::select_unit` (`0x754a20`), `unselect_all` (`0x755450`), `unselect_at_index` (`0x7554a0`), `unselect_object` (`0x755540`) fonksiyonları hook'lanarak `this + 0x160` yerine `g_custom_selection[player_id]` dizisine yönlendirilir.
- `player + 0x254` (seçim sayısı) 4-baytlık integer olduğundan 150 değerini doğal olarak destekler, yerinde kalabilir.

### Adım 2: Statik Global Arabelleklerin Genişletilmesi
1. `DAT_00de0430` (720 bayt -> 1800 bayt):
   - Kutu seçimi fonksiyonundaki `0x87d07d` ve `0x87d4fb` adreslerindeki `cmp ..., 0xde0704` kontrolleri, yeni tahsis edilen 1800 baytlık tamponun bitiş adresine yönlendirilir.
   - `0x87d57c` adresindeki `mov eax, 0x3c` değeri `mov eax, 0x96` (150) olarak yamalanır.
2. `DAT_00af4e40` (240 bayt -> 600 bayt):
   - `FUN_0074ef20` içindeki kopyalama hedefi yeni 600 baytlık tampona aktarılır.
   - `0x74f085` adresindeki `cmp eax, 0x3c` -> `cmp eax, 0x96` yapılır.
3. `DAT_00afb510` (240 bayt -> 600 bayt):
   - Komut yürütücülerindeki `0xafb50c` kontrolü (`0x3c` -> `0x96`) ve tampon adresleri güncellenir.

### Adım 3: `TRIBE_Command` Önbellek Yapısının Büyütülmesi
`command + 0x12c` üzerinde `last_commanded_units[9][60]` bulunur (`0xf0` bayt adım).
- `FUN_0080e6c0` (`rep stosd` ile temizleme) ve `FUN_0080dad0` (önbelleğe alma) fonksiyonları hook'lanır.
- 9 oyuncu için `150 * 4 = 600` baytlık yeni önbellek alanı tahsis edilir (`DWORD g_custom_last_cmd[9][150]`).

### Adım 4: Karşılaştırma Sabitlerinin Yamalanması (Binary Patches)
Aşağıdaki 1-baytlık sabit değişimleri `wl_patch` ile doğrudan bellek koruması kaldırılarak uygulanır:

```c
// 0x3c (60) -> 0x96 (150)
const uint8_t LIM_150 = 0x96;
const uint8_t LIM_149 = 0x95;

// Core Selection
wl_write_byte(0x754b25, LIM_150); // cmp eax, 0x96
wl_write_byte(0x754bb6, LIM_150); // cmp [esi+0x254], 0x96
wl_write_byte(0x755724, LIM_150); // cmp edx, 0x96 (unselect all loop)
wl_write_byte(0x755799, LIM_149); // cmp ecx, 0x95 (unselect shift loop)

// Drag Box & Hotkeys
wl_write_byte(0x87d57d, LIM_150); // mov eax, 0x96
wl_write_byte(0x86f8a6, LIM_150); // cmp eax, 0x96
wl_write_byte(0x822263, LIM_150); // cmp eax, 0x96
wl_write_byte(0x887749, LIM_150); // cmp eax, 0x96 (shift-select)
wl_write_byte(0x74f087, LIM_150); // cmp eax, 0x96 (group cmd copy)
wl_write_byte(0x74f080, LIM_150); // cmp [esi+...], 0x96 (ctrl+group)

// Command Handlers (10 adet fonksiyon)
wl_write_byte(0x80a4a2, LIM_150); // do_move
wl_write_byte(0x80a9a2, LIM_150); // do_attack
wl_write_byte(0x80b1b2, LIM_150); // do_work
wl_write_byte(0x80b752, LIM_150); // do_patrol
wl_write_byte(0x80ba52, LIM_150); // do_guard
wl_write_byte(0x80bd22, LIM_150); // do_follow
wl_write_byte(0x80c6a2, LIM_150); // do_explore
wl_write_byte(0x80cd52, LIM_150); // do_formation
wl_write_byte(0x80d722, LIM_150); // do_garrison
wl_write_byte(0x814fe2, LIM_150); // do_sell
```

### Adım 5: Kullanıcı Arayüzü Paneli (`tpnl_obj::draw_group`) Uyarlaması
- `0x71645f` adresindeki `cmp ecx, 0x3c` atlama kontrolü hook'lanır.
- Alt panelde 150 birim kutusunun sığması için iki seçenek mevcuttur:
  1. *Çoklu Sayfalama (Pagination / Tabs):* Panelde 60'ar birimlik sekmeler veya ok tuşları (`Next / Prev 60`).
  2. *Sadece İlk 60'ı Çizme:* 150 birim seçilir ve aynı anda hareket eder, ancak panel sadece ilk 60'ının can barını gösterir (Crash'i önlemek için en temiz ve risksiz başlangıç adımı).

---

## 7. Risk Analizi ve Kritik Uyarılar

1. **Ağ / Çok Oyunculu Paket Boyutu (Network MTU):**
   - 150 birimlik bir komut gönderildiğinde paket boyutu: `8 bayt başlık + 150 * 4 bayt = 608 bayt`.
   - DirectPlay / Steam P2P paketleri 1024-1400 bayt altındadır, bu nedenle 608 bayt tek bir UDP paketine rahatça sığar ve parçalanma (fragmentation) yaratmaz.
   - Ancak vanilya 5.8 istemcisiyle oynayan bir rakip 60'tan büyük bir paket aldığında `0xafb50c` taşması sebebiyle **senkronizasyon hatası (Out of Sync / OOS)** alacaktır. Bu özellik tüm oyuncularda Wololoader kurulu olmasını gerektirir.
2. **Formasyon ve Yol Bulma (Pathfinding Spikes):**
   - 150 birim aynı anda hareket ettirildiğinde formasyon algoritması (`baseworld.cpp` `FUN_00739b30` @ `0x739ca7`) CPU'da anlık donma (micro-stutter) yaratabilir. `0x739ca7` adresindeki sonsuz döngü güvenlik kontrolü de `0x96`'ya yükseltilmelidir.
3. **Shift-Click Stack Arabelleği:**
   - `FUN_00887660` fonksiyonundaki stack arabelleği 60 elemana göre ayrılmıştır. 150 birim shift-seçildiğinde stack patlamasını önlemek için bu fonksiyon detour ile hook'lanmalıdır.
