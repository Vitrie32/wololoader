# Wololoader — AoE2 HD reverse engineering notları

Hedef: `AoK HD.exe` (HD 5.8, kaynak dalı `age2hd_archive_5.7_with_hotfix`, 32-bit, ImageBase `0x400000`).
Oyun Wine/Proton altında çalışıyor (loglarda `Z:\run\media\...`).

## Araçlar
- Ghidra 12.2: `~/tools/ghidra_12.2_PUBLIC` (`JAVA_HOME=/usr/lib/jvm/java-21-openjdk`)
- Proje: `~/tools/ghidra_projects/AoE2HD`
- Pseudo-C dökümü: `re/out/decompiled.c` (15.465 fonksiyon), fonksiyon listesi `re/out/functions.tsv`
- Scriptler: `re/scripts/` (ExportAll, VtableDump, SymFind)
- Ghidra'nın RTTI analizi sınıf adlarını sembole çevirmedi. vtable'lar ham RTTI'dan çıkarıldı (`re/scripts/rtti.py`).
- Derleyici: `i686-w64-mingw32-g++` kurulu, yani proxy DLL burada derlenebilir.

## Dat yükleme
| Adres | Ne |
|---|---|
| `0x72ee50` | `loadDatFile()`: World/GameData yükleyici. `this[0x57]` = dat'tan okunan medeniyet sayısı, `this[0x56]` = medeniyet dizisi. Her medeniyet `FUN_0084add0` ile okunuyor. |
| `0x72ee50` çağıranı | `decompiled.c` satır ~506351 |

Sonuç: **Medeniyet sayısı dat dosyasından dinamik okunuyor.** Dat tarafında yeni bir medeniyet eklemek mümkün.

## Sabit kodlanmış medeniyet listeleri (asıl engel)
- `0x5ca530`: `CivType` reflection enum'u. Sıra şöyle: GAIA, BRITON … VIETNAMESE (0x1f), **NULL_CIV, RANDOM_CIV, SPECTATOR_CIV, MIRROR_RANDOM_CIV**.
  Yani 32 numaralı medeniyet `NULL_CIV` ile çakışıyor. Yeni medeniyetler için ya enum'un sonuna yer açmak ya da özel değerleri yeniden eşlemek gerekiyor.
- `0x4f5960`: AI script sembolleri için switch (`BRITON-CIV`…`VIETNAMESE-CIV`). Burada tanınmayan medeniyet id'si sessizce atlanıyor, yani çökmüyor ama AI `civ-selected` kontrolleri yeni medeniyeti tanımıyor.
- Sınıflar: `Civilization`, `DlcPackageCivilization`, `LimitedCivPool`, `CivilizationMode`.
- Yapılacak: `0x1f`/`0x20` sabitleriyle karşılaştırma yapılan bütün yerlerin listesi (medeniyet seçim ekranı, tech tree, isim/simge tabloları).

## Oyunun kendi mod sistemi
- `0x7cd3b0`: `ModManager` Step 2, yerel mod klasörlerini tarıyor (her klasör `0x38` byte'lık kayıt, paket nesnesi `0x440` byte).
- `NOMODS` komut satırı bayrağı var.
- Loglar: `Logs/<tarih>/Mod.txt`, `mod-status.txt`.
- Kaynak dosyaları: `modmanager.cpp`, `modpackage.cpp`, `gamedataset.cpp`.

## Ana menü (Wololoader yazısı için)
- `MainMenuScreen` vtable: `0x9d66a8` (59 slot)
- Constructor: `0x6e0c30` (`ui\main-menu\default` yerleşimini yüklüyor, kaynak `mainmenuscreen.cpp`)
- Yapılacak: constructor'daki metin etiketi (label) oluşturma çağrısını bulmak. Böylece sol alta aynı yöntemle "Wololoader vX — N mods loaded" etiketi eklenebilir. Bu olmazsa yedek plan d3d9 `EndScene` overlay'i.

## Önerilen Wololoader mimarisi
1. **Proxy DLL** (ör. `version.dll`; Wine'da `WINEDLLOVERRIDES="version=n,b"` gerekir). Oyuna dokunmadan yükleniyor ve `wololoader/mods/*` klasörünü tarıyor.
2. **Veri yaması:** Her mod küçük bir JSON/patch dosyası taşıyor. Oyun açılmadan önce orijinal dat'a uygulanıyor ve birleşik dat oluşturuluyor (dat formatı genieutils'teki yapıya göre).
3. **Kod yaması:** `CivType` sınırı, medeniyet seçim listesi, AI sembolleri ve isim/simge tabloları için hook'lar.
4. **Ana menü etiketi:** `MainMenuScreen` constructor'ına hook.

## Ana menü label (doğrulandı: statik analiz, çalıştırılmadı)
- Ana menü düzeni JSON'dan geliyor (`resources/_common/textures/ui/main-menu/default/main-menu.json`), ama JSON'da serbest label yok. Yazılar kodla oluşturulan `TextPanel` nesneleri (vftable `0x9b28a4`, boyut `0x210`, ctor `0x492c80`).
- **Label oluşturucu `0x48ff40`**: `__thiscall`, ecx = ekran (MainMenuScreen), 12 stack argümanı, `ret 0x30`, TextPanel* döner. Argümanlar: `parent(=ekran), const char* text, x, y, w, h, Font ref_ptr[4]*, f24, f28, f2c, strId(-1 = düz metin), f34`.
- Ctor içindeki örnek çağrı `0x6e19b7` (x=375 y=720 w=250 h=20, font id 0xa, bayraklar 1,0,0,-1,0); diğerleri `0x6e1ddf`, `0x6e1ec2`.
- Font dizisi: `0x472500(thiscall, ecx=ref_ptr[4], int fontId)` ret 4 (ctor 0xa ve 1 kullanıyor).
- Panel, taban ctor `0x482b10` içinde ebeveyne kendini ekliyor (`0x4836b0`). Sonradan ayrıca bir şey yapmak gerekmiyor.
- Koordinatlar "referans" birimi; `0x491190` ekranın ölçeğiyle (`ekran+0xc0/+0x130`, `+0xc4/+0x134`) pikselle çarpıyor. Referans boyutu = ekran oluşturulurken pencerenin `GetClientRect` boyutu (`0x456060`), ekran+0x130/+0x134'te. Sol alt için: x=10, y=*(int*)(ekran+0x134)-28.
- Metni sonradan değiştirme: `vtbl[54]` (+0xd8) = `0x497040(this, const char*, 12 bayt boş vector)` ret 0x10. Boş vector = 3 adet 0 dword (`0x48ff40` de böyle çağırıyor).
- Hook: `MainMenuScreen::ctor 0x6e0c30` (`__thiscall(this, a, b)`, `ret 8`, this döner). İlk 5 bayt `55 8b ec 6a ff` (relocation gerektirmez). Nesne 0x5f4 bayt, hep `(0,0)` argümanlarıyla yaratılıyor.
- Yedek plan: `TDrawSystem*` global `0xad20b4`, D3D9 device = `*(IDirect3DDevice9**)(TDrawSystem+0x48)` (`CreateDevice` çağrısı `0x453c10`, EndScene çağrısı `0x4548b0`/`0x455940` içinde vtbl+0xa8). IDirect3D9 = `TDrawSystem+0x44` / `0xad20a8`.

## Ana menü etiketi — oyunda doğrulandı (2026-11-14)
Sadece `0x48ff40` çağırmak yetmiyor, etiket görünmez kalıyor. Constructor'daki sıranın aynısı gerekiyor:
1. `screen->0x48d390(&pair, &pair)` (ret 8); `pair = {*(0xada500), *(0xada504)}`, çalışma anında `00000000 00bf0000`
2. `screen->0x48d3c0(0)` (ret 4)
3. `0x48ff40(...)` ile paneli oluştur
4. `screen->0x48d550(panel)` (ret 4); panel `+0x144` ve `+0x15c` değerlerini 2 yapıyor (görünür durum)
1366x768 ekranda `ref` ile `cur` aynı. Etiket `(10, ref_h-28, 360, 20)` konumunda, font 0xa. Uygulaması `wololoader/src/menu.c` dosyasında.

## Civ sınırları (2. tur, doğrulananlar)
Özet: Medeniyetler artık sabit dizi değil, **çalışma zamanı kayıt defterinde** (global `std::map<int,Civilization*>` @ `0xaf25ec`). Asıl engel özel değerlerin (RANDOM=32...) 32+ id'lerle çakışması ve birkaç `1..31` aralık kontrolü.

### Özel civ değerleri (doğrulandı, `0x5ca530` push'larından)
`NULL_CIV=-1 (0xFFFFFFFF)`, `RANDOM_CIV=0x20`, `SPECTATOR_CIV=0x21`, `MIRROR_RANDOM_CIV=0x22`. (Önceki notta NULL=32 yazıyordu, yanlış.)
- `CivilizationMode` (4 öğe, `DAT_00af28c0`, static init `0x41e0f0`): mode0 = specific (değer 0), mode1 = random `push 0x20` @0x41e21b (metin 0x281e), mode2 = spectator `0x21` @0x41e2b2 (0x281d), mode3 = mirror `0x22` @0x41e349 (0x281c). `0x59df50(idx)` bu tabloyu okur (idx<4).
- `0x607490` `CivChoice::setFromInt(int)`: `cmp edi,0x20/0x21/0x22` @0x6074c9/0x607540/0x607586; diğer değerler mode0 + `map.find(id)`, bulunamazsa Briton(1). Çağıranlar sabit gönderir: `push 0x20` @0x60fac8, `push 0x21` @0x6159b1.
- `0x607820` `toInt()`: mode==0 ise `civ->id` (+8), değilse `mode+0x20`. `0x607840`: aynı ama spectator için 0x21.
- Dropdown özel girdiler `0x65e700` içinde: `push 0x20` @0x65eb8d, `0x22` @0x65ec20, `0x21` @0x65ecc0; `0x65f050` `mov [edi+0x8d0],0x22/0x20` @0x65f14b/0x65f1ff; `0x59f5d0` `mov [esi+8],0x20` @0x59f5f6; `0x615540` `cmp eax,0x21` @0x615574; `0x75ef60` `cmp eax,0x21` @0x75f01e.
- Öneri (en az yama): yeni civleri **id 35+** ile ekle (32-34 dat'ta yer tutucu). Özel değerlere dokunma.

### Civ kayıt defteri (yeni civ eklemenin temiz yolu)
- `0x5c9640` (cdecl): `reg(pkg, id, "x.mp3", nameStrId, bonusBaseStrId, uniqueTech1, uniqueTech2, playable)`. İçeride `0x59c990` ile `0xaf25ec` map'inde arar, yoksa `new Civilization(0x3c)` (ctor `0x5c9330`, alanlar: +8 id, +0xc mp3, +0x10 isim string id, +0x14 bonus-string tabanı, +0x18/+0x1c iki unique-tech id, +0x20 playable byte), sonra `DlcPackageCivilization(0x24)` yaratıp paketin listesine ekler (+0x10 = `0x4eb5+id` veya `0x1d555+id`, açıklama string id'si, paket bayrağına göre).
- Çağıran sarmalayıcılar (paket başına): `0x5c9d00` (Gaia + 1..13), `0x5c9a30` (14..18), `0x5c9af0` (19..23 Forgotten), `0x5c9bc0` (24..27 African K.), `0x5c9c60` (28..31 Rajas). Hepsi `FUN_005d8ac0` (DLC listesi kurucusu) içinden çağrılıyor: 0x5d9991/0x5dad54, 0x5d9edb/0x5dafe5, 0x5db2bd, 0x5db8e4, 0x5dbda0. Hook noktası: bu sarmalayıcılardan birini detour edip orijinali çağırdıktan sonra yeni civler için `0x5c9640(pkg,...)` çağır.
- Unique tech id'leri global vektöre (`0xaf25e0..e4`) da yazılıyor (`0x5ca0e0` üyelik kontrolü, `0x70ee80` kullanıyor). Briton için (3, 0x1cd=461) = Yeomen/Warwolf (kendi bilgime göre, kesin değil).

### İsim string id'leri (doğrulandı)
- `0x62f080` `civNameStrId(int)`: `0->0x2776 (10102)`, `1..31 -> map.find(id)->[+0x10]` (`lea eax,[ecx-1]; cmp eax,0x1e; ja` @0x62f0af), `0x20->0x281e (10270)`, `0x21->0x281d`, `0x22->0x281c`, aksi `-2`. Kayıtta isim id'si = `10270+id` (Briton 0x281f=10271 ... Vietnamese 0x283d=10301). Yani id 35 için 10305 doğal seçim, ama 10302..10304 boş kalır; ya da kayıt sırasında istediğin id verilir.
- Bu fonksiyon `0x62f430` (UI metin sağlayıcı, vtable `0x9ceec4`, `case 0x69`) tarafından çağrılıyor. **YAMA GEREKİR:** id>=35 için `-2` döner (boş metin). `0x62f0af` kontrolünü `ja` -> genişlet (ör. `cmp eax,0x1e / ja` yerine `id` 35+ de kayıt defterine yönlenecek şekilde).
- Civ bonus listesi: `0x5c98b0(civ,n)` = string `[civ+0x14]+n+1`; `0x5c9970` = `atoi(string([civ+0x14]))` (bonus satır sayısı). Briton 4400, her civ +20 (ör. Khmer 0x19e60). Yeni civ için 20 ardışık string lazım.

### Oyun kurma dropdown'u (doğrulandı, sınır YOK)
`0x65e700` (8 slot, `param_1+0x2e8+i*0x70`): `0x75ef60` ile slota uygun `Civilization*` vektörü kurar, her biri için `itoa(civ[+0x10])` -> metin -> `0x47b480(listbox,metin,ikon,değer=civ id)`; sonra Random(0x20), Mirror(0x22), Spectator(0x21). Öğe sayısı vektörden geliyor, sabit üst sınır görmedim. İkon civ'e değil **DLC paketine** bağlı (`ui\*-icon-16x16.png`, paket tablosu `0x422250`: basegame/kings/conquerors/forgotten/dlc2/dlc3), özel girişler `ui\random|mirror|spectator-icon-16x16.png`.
- Başlatmada rastgele civ çözümü `0x75f5a0`/`0x75ed90`/`0x760160` (launchlevel.cpp), vektör tabanlı, sabit dizi yok.
- `0x607860` (çağıran `0x61e240`): seçili civ `0x5c97d0(vec,3)` listesinde değilse Briton'a düşer. `0x5c97d0` = playable(+0x20) ve sahiplik düzeyi >= 3 olan civler.

### DLC kapısı
- Paket kurucusu `0x5d2620` (0xbc byte, vtable `0x9c97e8`), paket indeksleri 0..6 (retail1..5 + dlc3). Sahiplik `0x46fd10`: `pkg+0x40` bayrağı doluysa `pkg+0x44` değeri döner (zorlama), yoksa Steam `SteamApps()` ile 3 (sahip) / 2 / `(pkg[2]==0)`. Civ erişimi = civin paketlerindeki max sahiplik (`0x5c99d0`) >= 3. **Yükleyici için:** yeni civ paketinin `+0x40=1, +0x44=3` yapılırsa DLC sahipliği devre dışı kalır (bu kurulumda zaten Steam'siz).
- `LimitedCivPool` (`0xa0` byte, kurucu `0x5dd8d0/0x5dd990`, doldurma `0x5ddae0`): seed `% 0xb4`, paket bayraklarına göre 5-7 civ seçer, sayaç dizisi `+0x14..+0x9b` (34 int) -> en çok 34 öğeli listeye kadar güvenli (liste öğe sayısını doğrulamadım, tahmin).

### Sert sınırlar / zararsız olanlar
- `0xaae130` (32 int, alfabetik sıra -> civ id) yalnız Tarih ekranı `0x6cd400` (`cmp esi,0x1f` @0x6cd7cc, metin `0x514b+index`, `history\%s.txt`): ihtiyaç bunu yeni civlerde kozmetik; >=32 ise `random.mp3`.
- İstatistik/başarım: `0x5bf4ba` (`cmp al,0x1f`, tablo `0xaad6a4`), `0x5bfe33` (tablo `0xaad5c8`): `civ<0x20` korumalı, yeni civ atlanır, çökmez. "Her civ" başarımı döngüleri 32'de sabit (`0x3a..0x5a`, `3..0x23`), yine bozulmaz. Oyuncu kaydında civ **byte** (+0x53, +0x1f).
- AI: `0x4f5960` jump table'ları (`cmp eax,0x1e` @0x4f5d4b, `cmp eax,0x1f` @0x4f660f) yalnız sabit sembol/tanım için, >=32 sessizce atlanır. `my-civ`/`civ-selected`/`players-civ` ham int döner, çalışır.
- `loadDatFile` civ dizisini sayıya göre ayırıyor (`0x8ea479(count,4)` @0x72ee50), sabit üst sınır yok.
- `CivType` reflection enum'u (`0x5ca530`, isim hash -> değer): yalnız metinden okunan ayarlar için; yeni civ girilmezse bu yoldan tanınmaz.
- Sert kodlu civ özel durumları: `0x70ee80` (`cmp [x+0x2d8], 0xb/0xe/0x12/0x13/0x15/0x18/0x1b/0x1f`), semantiği doğrulanmadı.

## Özel medeniyet — oyunda doğrulandı (2026-11-14, Wololoader v0.2)
| Adres | Kullanım |
|---|---|
| `0x448370` | string tablosu `thiscall(table, const char* key) -> const char*`, ret 4. Hook'lanıyor, 9000000+ id'leri bizim metinler. |
| `0x5c9c60` | Rajas kayıt fonksiyonu, cdecl(pkg). En son çalışıyor. Hook'ta orijinal çağrıldıktan sonra `0x5c9640` ile 35+ kaydediliyor. |
| `0x62f080` | civ id -> isim string id. 35+ için hook. |
| `0x6c5aa9` | `game_b%d.slp` civ id'si. Özel civ'de base civ kullanılıyor (6 bayt call yaması). Yamasız `0x45a496`'da çöküyor. |
| `0x5d63e0` | DLC civ sayısı, sadece tech tree (`0x68bcf0`, `0x84f860`) çağırıyor. En büyük özel id'ye yükseltiliyor. Yamasız `0x84fd7b`'de çöküyor. |
| CreateFileA/W IAT | `empires2_x2_p1.dat` -> `wololoader\cache\` yönlendirmesi |
Dat: ses kayıtlarındaki `civilization` alanı yeni civ için kopyalanmalı, yoksa birimler sessiz kalıyor.

## Mods ekranı (statik analiz, çalıştırılmadı; 2026-11-14)
Yardımcı: `re/scripts/UiBytes.py <exe> <n> <va...>` (VA'daki ilk n bayt).

### 1. Ana menü tık dağıtımı
- Global `0xaf3eec/0xaf3ef0` (vector begin/end, 16 bayt kayıt `{id, const char* name, flag, handler}`) static init `0x423580` ile dolar: about(0,h `0x6e8c00`), single-player(1,`0x6ea280`), multi-player(2,`0x6e95f0`), tutorial(5,`0x6ea520`), history(4,`0x6e9210`), options(7,`0x6e8d70`), **mod-manager(id 8, flag 0, handler `0x6e9380`)**, map-editor(3,`0x6e9ed0`), exit(6,`0x6e90c0`). İsim stringleri `0x9d6640..0x9d669c`.
- Ctor `0x6e0c30` -> `0x6e8430(screen, layout)` her kayıt için `0x6e7da0(id, flag, layoutDef, handler)` ile buton yapar (`ButtonDrawableControlPanel`, vtbl `0x9b1534`), buton `screen+id*0xc+0x298` (ref_ptr). mod-manager butonu = `screen+0x2f8`.
- Tık -> functor (vtbl `0x9d6da8`, slot2 `0x6e44b0`) -> `0x6e9af0(screen; id, flag, str*, handler) ret 0x10` -> `mov ecx,screen; call handler` (0x6e9ca8). Yani handler **`__fastcall(MainMenuScreen* ecx)`, `ret` (arg yok)**.
- `0x6e9380` = mod-manager handler: `new 0x390; memset; ModManagerScreen ctor 0x6ee030(ecx=obj, push parent); ecx=[0xada4b4]; push obj; call 0x48a8e0`. Bayt: `55 8b ec 6a ff 68 02 10 96 00` -> **hook: `wl_hook(0x6e9380, fn, 5)`**, orijinali çağırma (tamamen değiştir). Başka çağıran yok (fonksiyon göstericisi tek yerde).
- Buton kimliği gerekmez: handler'ın kendisi benzersiz. (Buton yazısı `STEAM_WORKSHOP` anahtarı; istenirse `0x448370` hook'unda "Mods" döndürülür, doğrulanmadı.)

### 2. Ekran yönetimi
- İki global yönetici: `[0xada4b4]` = ekran listesi/yığını, `[0xada3f0]` = isimle ekran kaydı.
- `0x48a8e0` thiscall(ecx=[0xada4b4], screen) `ret 4`: ekranı en üste alır (öncekini `0x484c30(0)` ile pasifler, `screen+0x258`=listede bayrağı, `0x48aad0` ile gösterir).
- `0x48a930` thiscall(ecx=[0xada4b4], const char* name) `ret 4`: isimle bulup set-current (ana menüye dönüş: name `"Main Menu"` = `0x9d09b4`, MainMenuScreen adı).
- `0x485e00` thiscall(ecx=[0xada3f0], const char* name) `ret 4`: isimle bulup `vtbl[0](1)` ile siler.
- Silme: ScreenPanel dtor body `0x489dc0` (çocukları siler, `0x48a9f0` listeden çıkarır; silinen current ise listenin yeni tepesini `0x48aad0` ile yeniden aktif eder) => **ekranı silmek ana menüye dönmek demek**, ayrıca set-current gerekmez.
- Güvenli kapanış (tık handler'ının içinden, buton ölmesin): oyunun ModManager "Close" yolu `0x4382e0(std::function*)` (cdecl, 1 arg) ertelenmiş çağrı kuyruğu; functor sınıfı vtbl `0x9d7e18` (`[0]=0x9d7e18,[4]=screen,[0x24]=&kendisi`, 0x28 bayt), slot2 `0x6a5390` = `screen->vtbl[0](1)`. 0x6f17a0 bunu stack'te kurup `0x4382e0` çağırıyor.

### 3. Ekran sınıfı seçimi
- `ScreenPanel` (vtbl `0x9b24ac`, 59 slot) **boyut 0x27c** (dtor `0x489e80` `0x8d480f(this,0x27c)`). Ctor `0x489d50` thiscall(ecx=obj, `const char* name`) `ret 4` (isim `screen+0x58`, işaretçi saklanır -> statik string ver; kendini `0x48a9e0` ile yöneticiye kaydeder). Düz örnek: "Blank Screen" `0x541ec0` = `new 0x27c; memset; ctor(name 0x9c02c8); 0x48a8e0`.
- Tercih (b): kendi vtbl kopyası (59 dword kopyala, slot 49 değiştir). Nesne `operator new 0x8d47df` (cdecl) ile oyun heap'inden alınmalı (dtor `0x8d480f` ile siler); memset `0x8d62f0` cdecl.
- **Slot 49 (+0xc4)**: `thiscall(ecx=screen; panel*, int code, int p3, int p4) ret 0x10`; varsayılan `0x48d430`. Butonlar (`0x479xxx`, 0x4796be/0x479631) `parent=[btn+0x10]` -> `[vtbl+0xc4](btn, code, ..)`; **code 3 = sol tık bırakma**, 2 = basma, 5/6 sağ tık, 1 = liste seçimi. İşlenmişse 1 döndür, aksi halde orijinal slot 49'a ilet. ModManager `0x6f17a0` aynen böyle (`param_2==screen[0x2c0]` işaretçi karşılaştırması).
- Arka plan: `0x48a1c0(ecx=screen, const char* name, int id, int 0) ret 0xc` (-> `0x4917b0`). ModManager: `("scr2p", 0xc3a4, 0)` (dosya `resources/_common/slp/scr2p_32.slp`); CampaignMenu `("scr3", 0xc385, 0)`.
- Ardından `0x491250(ecx=screen, int[2]* {w,h}) ret 4` referans boyutu (`+0x130/+0x134`), ModManager `{0x556, 0x300}` (1366x768). Verilmezse ölçek bölmesi bozulabilir (tahmin).
- Stil dizisi ModManager'da `0x6ee1b1..0x6ee1e4` ile aynı (`screen+0x194..0x1a4`), yani ana menüdeki 0x48d390/0x48d3c0 ile eş.

### 4. Butonlar
- `0x48df30` **ham metin butonu**: `thiscall(ecx=screen)` + stack `(screen, const char* text, const char* text2|NULL, x, y, w, h, 0, 0, 0)`, `ret 0x28`, `ButtonDrawableControlPanel*` döner. Metin kopyalanır (çağıranlar 0x78'lik stack tamponu kullanıyor). Örnek `0x6ed279`: `push 0,0,0, 0x20(h), 0x6e(w), 0x20d(y), 0x96(x), 0(text2), str, edi; ecx=edi`. Son üç arg font/ses vb. (hepsinde 0; anlamları doğrulanmadı).
- `0x48ddf0` = string-id'li sarmalayıcı (stack: screen, strId, strId2, x,y,w,h,0,0,0 + 0x28 bayt std::function değer olarak, `ret 0x50`); About ekranı `0x69d759`.
- Bağlama: tercih (b)'de buton işaretçisini sakla, slot 49'da `code==3 && panel==btn` kıyasla. Alternatif: `0x4667c0(ecx=btn+0x5d0, std::function*)` ile functor (oyunun About/diğer ekranları).
- Odak zinciri (opsiyonel): `0x484fd0(ecx=screen; panel** arr, short n)` ve `0x484c90(ecx=screen; panel)` ret 4.
- Metin etiketi (ekran içi): `0x48fe60` `thiscall(ecx=screen)` stack `(screen, TextPanel** out, const char* text, x,y,w,h, int fontId, f24, f28, f2c)` `ret 0x2c`, `*out` doldurur (içerde font dizisi + `0x48fdb0` -> `0x48ff40`). Örnek ModManager başlık: `x0,y0xf,w0x556,h0x28,font1,(1,1,0)`; About: font 0xb, (0,0,0); ana menü 0xa (1,0,0).

### 5. Görünürlük / per-frame
- Butonlar için ayrı stil çağrısı görmedim (About/ModManager'da yok); yalnızca TextPanel için ana menüdeki 0x48d390/0x48d3c0/0x48d550 dizisi (oyunda doğrulanmış). Per-frame gereksinim yok (çizim slot 15 `0x489f80` + çocuklar).
- Slot 48 (`+0xc0`) aktif/pasif bildirimi (`0x484c30` çağırır), slot 17 (+0x44) tick.

## Mods ekranı v2 (resim/metin) — statik analiz, çalıştırılmadı; 2026-11-14
Yardımcılar: `scripts/UiRet.py <full.asm> <va..>` (ret N listesi), `scripts/UiImgPanel.py <full.asm>` (zincirdeki tüm fonksiyonların ret N'i), `scripts/UiVtbl.py <exe> <vtbl> [n]` (vtbl dökümü).

### A. PNG dosyasından resim paneli (DLC banner yolu, `0x6e5400`)
- Sınıflar (RTTI): `ui::ImageFactory` ctor `0x4979a0` global `0xada4e4`; yükleyici zinciri = 12 baytlık vector global **`[0xada9f4]`**, tek eleman `ui::ImageFileDrawableLoader` (vtbl `0x9b29ec`, slot2 `0x476cb0` -> `0x4763e0`). `DrawablePanel` (vtbl `0x9b1920`, **boyut 0x134**).
- **Yükle**: `0x49f1f0` = `__thiscall(ecx=*(void**)0xada9f4, void** outDrawable, const char* path)`, **ret 8**, out'u döner (içeride `*out=0` yapar; hata -> out NULL kalır, log "Failed to load texture"). Zincir `0x49ee10` (ret 0xc, loader vtbl+8).
- **Yol çözümleme**: yükleyici `path`'i **olduğu gibi** `D3DXCreateTextureFromFileExA(DAT_00ad20ac, path, 0xffffffff, 0xffffffff, 1, 0, fmt 0x15 A8R8G8B8, pool 2, ...)`'e verir (`0x4763e0`), cache anahtarı da bu string (`0x476020`). Yani ham dosya yolu: **mutlak yol (Z:\...) çalışır, resources altında olmak zorunda değil**; göreli ise process CWD'ye göre. DLC banner'da önce `0x43a500` ile {storage-id 0x19, "ui\main-menu\default\dlc\dlcN.png"} -> gerçek yola çevriliyor (`0x828670`: storage 0 = ham yol); biz bunu atlayıp doğrudan mutlak yol veririz. Dosya biçimi: D3DX'in okuduğu her şey (PNG ok). Risk: ANSI API, yolda `ü` gibi ASCII dışı karakter Wine'da sorun çıkarabilir (doğrulanmadı); 2'nin kuvveti boyut (16/32/64) tercih et.
- **Panel**: `p = game_new(0x134); memset(p,0,0x134); 0x47a5d0(ecx=p, parent=screen, int rect[4]={x,y,w,h}, drawable)` **ret 0xc**, p'yi döner. Ctor ebeveyne kendini ekler (taban `0x482b10`), `screen` doğrudan ebeveyn. `drawable` (+4 refcount) panel tarafından "by-value ref" gibi tüketilir (ctor net 0): yükleyiciden gelen ref'i olduğu gibi geçirmek = sahipliği panele devretmek (banner kodu da inc + kendi ref'ini bırakıyor, eşdeğer). Taze yaratılmış (rc=0) nesne için önce `rc=1` yap, yoksa ctor silebilir.
- **Koordinat**: label fabrikası rect'i `0x491190(ecx=screen, int out[4], const int in[4])` ret 8 ile ref->piksel çeviriyor; DrawablePanel ctor çevirmez. Aynı davranış için aynı fonksiyonla çevir (1366x768 ref=piksel iken gerekmez).
- **Ölçekleme**: çizim (vtbl slot15 `0x47a760` -> drawable vtbl[2] `0x475970` -> `0x498550`) 3 alanlı mod yapısı kullanır, drawable `+0xc=mode, +0x10=vAlign, +0x14=hAlign`; varsayılan `{0,1,1}`. mode 0 = doğal boyut, 1 = oran koruyarak sığdır, **3 = rect'e esnet**, hizalama 0 sol/üst,1 orta,2 sağ/alt. Ayarla: `drawable->vtbl[3]` = `0x476e80` `thiscall(drawable, int p[3]={3,1,1})` **ret 4** (panel ctor'dan önce veya sonra; her yüklemede yeni ImageFileDrawable yaratılıyor, paylaşımlı değil). Doğal boyut: `vtbl[4]` = `0x4761e0` `thiscall(drawable, int wh[2])` ret 4.
- Tarif: `chain=*(void**)0xada9f4; void* d=0; ((LoadImg_t)0x49f1f0)(chain,&d,"Z:\\...\\icon.png"); if(d){int m[3]={3,1,1}; (*(void***)d)[3] -> SetFit(d,m); int r[4]={x,y,w,h}; ScaleRect(screen,rs,r); p=new(0x134); memset; DrawablePanelCtor(p,screen,rs,d);}`
- Frame/arka plan (opsiyonel): `ui::SolidColorDrawable` ctor `0x4a0350(ecx=obj(0x10 bayt), const Color* 8 bayt)` ret 4 (slot2 `0x4a0430` = rect doldur) DrawablePanel'e verilebilir, ama Color formatı doğrulanmadı (`0xada500/0xada508...` adlandırılmış renk global'leri; `0x414b40`). Ucuz yol: kendi PNG'ni (kenarlıklı) mod 3 ile esnet.

### B. TextPanel (vtbl `0x9b28a4`, 60 slot)
- **SetText = slot 54 (+0xd8) `0x497040`**: `thiscall(panel, const char* text, int z0, int z1, int z2)` **ret 0x10** (doğrulandı; 3 dword = by-value boş `std::vector`, sıfır ver). İçeride slot 59 (+0xec, `0x4936b0`: satırları sil, relayout bayrağı) sonra slot 57 (+0xe4, `0x495f40`, aynı imza ret 0x10) çağırır. Metin kopyalanır; çizim her karede (slot 15 `0x496260`) satır listesinden -> **ek layout/redraw çağrısı gerekmez**. "" = tek boş satır.
- **Satır sonu**: `0x495f40` metni '\n' (10) üzerinden böler (her satır ayrı satır nesnesi) -> `\n` hep çalışır.
- **Word wrap**: `+0x1ac` baytı (= `0x48fe60`'nin **f2c** argümanı -> `0x495790`) 0 ise sadece '\n'; **≠0 ise `0x4973a0` kelime kaydırma** (\r \n ve boşlukta kırar, genişlik = panel genişliği-10 px, ikili aramayla). Uzun metin için f2c=1, yüksekliği satır sayısına yetecek ver.
- **f24 / f28** (`0x48ff40` içinde `0x4951d0(f24^1, f28^1, 0)`): `f24^1` -> `rec[0]+0xc` -> `0x494be0` bayrakları: 0 -> 5 (DT_CENTER|DT_VCENTER), 1 -> 4 (DT_LEFT|DT_VCENTER), 2 -> 6 (sağ). Yani **f24=1 yatay ORTALA, f24=0 SOLA** (About f24=0 kullanıyor). `f28^1` -> panel `+0x198`; `0x493f70` içinde 0 ise metin bloğu **dikey ortalanır**, ≠0 üstten başlar: **f28=1 dikey orta, 0 üst**. Güven: orta-yüksek (bayrak değerleri D3DX DT_* ile uyumlu; oyunda denenmedi). Mevcut ui_mods.c `(1,center,0)` -> satırlar yatayda ortalı çıkıyor olabilir; sola yaslamak için f24=0 dene.
- **Renk**: panelde 3 stil bloğu `+0x134/+0x14c/+0x164` (0x18 bayt: `{Color ana(8), Color gölge(8), mode, flag}`; ctor ana=`0xada508`, gölge=`0xada500`, `0x48d550` mode=2 (gölgeli) yapar; `0x4a0b20` mode'a göre çizer). Color 8 bayt (iki dword), format ve hangi blok "normal" belli değil -> **doğrulanmadı**. Seçili satır vurgusu için renk yerine buton SetChecked (aşağıda) öneriyoruz.

### C. Buton durumu / görünürlük
- **Kalıcı "seçili"**: `0x477450` `thiscall(button, char checked)` **ret 4** (non-virtual; `[btn+0x16c]=checked`). Ana menü tık işleyicisi `0x6e9af0` önceki butonu `SetChecked(0)`, yenisini `SetChecked(1)` yapıp `screen+0x27c`'de id tutuyor (`0x6e9c26/0x6e9c5a`). Çizim `0x4772f0`: fare üstünde değilken state = checked ? 1 : 0 (hover: checked?2... ), yani basılı/seçili görüntü kalıcı. 0x48df30 butonu da ButtonDrawableControlPanel olduğu için geçerli (statik; oyunda denenmedi).
- **Buton metni değiştirme (SetText)**: `0x479ce0` `thiscall(ecx=button, int slot, const char* text)` **ret 8**. Oyun içi ModManager (`0x6ef720`, `0x6ef780`, `0x6ef800`) tarafından buton yazılarını (Disable/Enable vb.) değiştirmek için kullanılır. slot 0 ana yazıdır. `std::string` kopyalaması yapar, eski metni güvenle serbest bırakır.
- **Enable/disable**: `ButtonPanel` vtbl slot 7 (+0x1c) `0x479840` `thiscall(btn, char disabled)` **ret 4** (`+0xc9`, `+0x24`). (Önceki notta slot 61 yazılmıştı, vtable dökümünde slot 7 olduğu doğrulandı).
- **Gizle/göster**: Panel `+0x1c` (int, ctor `0x482b6d`'de 1) = görünür/aktif bayrağı: ekran çizimi `0x490490`, çocuk çizim sarmalayıcısı slot 20 `0x4845d0`, input `0x483df0/0x4840b0/0x4841a0` ve tick `0x4847e0` hepsi `[this+0x1c]==0` ise atlıyor. Ayrı bir setter fonksiyonu yok; doğrudan `*(int*)(panel+0x1c)=0/1` yaz. Güven: orta (statik, denenmedi). Alternatif tarif: satırları yeniden yaratmak yerine ihtiyaç kadar etiket yaratıp SetText kullan.
- Fokus: `+0xc8` = fokus bayrağı (tuş işleyici slot 22 `0x483f80` buna bakıyor), `0x484c30` ile ayarlanır.

## Nüfus limiti (pop cap 1000) — 2026-11-14, agy araştırması + Claude doğrulaması

- `0x47b690` dropdown AddItem: `__thiscall(list, const char *text, int value)`, `ret 8` (doğrulandı).
- `0x64d25b..0x64d33c` oyun kurulum ekranı nüfus listesi: döngü 25..200 (`cmp esi,0xc8` @0x64d2b8), sonra 250/300/400/500 elle; "500" ekleyen çağrı `0x64d337` (`e8`, 5 bayt).
- `0x6da131` lobi tarayıcı filtresi: aynı yapı, "500" çağrısı.
- `0x5961fe` `mov ecx,0x1f4; cmp eax,ecx; cmova` FUN_005960a0 içinde nüfus 500 kırpma.
- `0x748068..0x748098` oyuncu kaynak 32 (bonus nüfus limiti, `[eax+0x80]`) 25..500 aralığına kırpılıyor: `comiss xmm1,[0x9b50e0]` @0x748079, `mov [eax+0x80],500.0f` @0x748082.
  `0x9b50e0` (500.0f) kare süresi kırpmasıyla (`0x6c1f31`) paylaşılıyor — değiştirme, comiss operandını yönlendir.
- `0x750fd1` FUN_00750f70 (kaynak ayarlayıcı): `[this+8]==1` olan oyuncuda kaynak 4 (nüfus boşluğu) her ayarlandığında 500.0 yapılıyor. Hun bonusu (kaynak 4 = 1000) burada 500e kesiliyordu (oyunda 504/504 görüldü) → 1000.0 yapıldı.
- `0x52c640` AI birim toplama: 500 elemanlı yığın dizileri (`cmp esi,0x1f4` @0x52c820, @0x52ca92) — ASLA büyütme.
- `0x637680` `/LIMIT` komut satırı varsayılanı 200 — dokunulmadı.
Uygulama: `wololoader/src/popcap.c`.

## Çok oyunculu veri kontrolü + benzersiz birim (2026-11-14, agy; özet, detay re/research/)
- Lobi, oyun verisinin CRC32'sini karşılaştırıyor: `0x584a40` host işleyicisinde istemcinin `paket+0x18` değeri ile karşılaştırma, uyuşmazsa `0x583f50(player, reason=3)` → "GAMEDATA_CRC_CHECK" (doğrulandı, decompile). İstemci tarafı `GAMEDATA_CRC_CHECK_HOST` (`0x6566dd` civarı, GUESS). Sonuç: aynı Wololoader modları = aynı dat = aynı CRC → birlikte oynanabilmeli (oyunda test edilmedi).
- Steam lobi verisi "Dataset" / "DatasetWorkshopId" (`0x5b5310` içinde SetLobbyData), dizeler exe'de mevcut (doğrulandı).
- Yeni birimler için dat'ta boş yuva: birim 1221 ve 1224..1400 adı "RESERVED" (datfile.py ile doğrulandı, civ 1). Oyunun bazı dizileri 1400 (`0x578`) ile sınırlı → yeni birimleri bu yuvalara koy.
- Kale benzersiz birimi: `train_location_id=82`, `button_id=1`, civ'e özel teknoloji (civ alanı) ile effect type 2 açılıyor; teknoloji ağacı ekranı kale birimlerini veriden alıyor (agy, kısmen doğrulandı).

## Metin kodlaması (2026-11-14, donma bekçisiyle bulundu)
- Oyunun metin sistemi UTF-8. `0x444e40` UTF-8 çözücü: geçersiz baytta ilerlemeden 0 döner; çağıran `0x4936e0` (`SetText` 0x497040 → 0x4973a0 → 0x4936e0) sonsuz döngüye girer → oyun donar (teknoloji ağacında açıklamadaki tek 0xB7 baytı).
- Eski "Â·" hatası json.c'nin çift kodlamasıydı; Windows-1252 çevirisi kaldırıldı, civs.c artık geçersiz UTF-8 baytlarını '?' yapıyor.

## Evet/Hayır penceresi kapatma (2026-11-14)
- `0x490da0` ile açılan pencere kendiliğinden kapanmıyor. Ebeveyn, olay slotunda (slot 49) cevabı aldıktan sonra `0x485e00` `__thiscall(*(void**)0xada3f0, const char *name)` `ret 4` ile ismiyle kapatıyor (ana menü "WorkshopTOS" için `0x6e4cde`). code 0 = Evet.

## Metin id'leri, UI dokuları, Workshop (2026-11-14, oyunda doğrulandı)
- Birim düğmesi zengin tooltip: oyun `creation id + 20000` ister (Teutonic Knight 6112 -> 26112). Bizim birimde 28501 -> 48501 log'da görüldü.
- Tech tree tooltip: `0x708d50` içinde `mov eax,[unit+0x270]; add eax,0x5208` (0x708e02) -> `name id + 21000`. Log'da 49500 görüldü.
- Tech tree, araştırmalar için `X` ve `X+10000` çiftlerini ister (ör. 7410 / 17410).
- Arayüz resimleri (ana menü butonları vb.) `d3dx9_43!D3DXCreateTextureFromFileExA` ile yüklenir (exe import'u), CreateFile hook'larından geçmez.
- `0x7ce430` = `NOMODS` komut satırı bayrağının okuyucusu (`a0 18 59 af 00 c3`, `mov al,[0xaf5918]; ret`). Çağıranlar 0x5c34a8, 0x7cd48b (`test al,al; je`). `mov al,1; ret` yapınca ModManager/Workshop senkronu atlanır ("Commandline NOMODS detected").
- Açılıştaki donma: ana iş parçacığı `0x454dae` `call [ecx+0x44]` (IDirect3DDevice9::Present) içinde bekliyordu, Workshop diyaloğu açıkken; NOMODS ile bir daha görülmedi.
