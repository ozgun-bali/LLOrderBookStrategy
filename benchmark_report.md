# Benchmark Raporu

`itch_data_250815_HI2.dat`, 584.628.619 byte, book 73616 (`EKGYO.E`),
veriyle birlikte verilen konfigürasyonla.

## Ortam

| | |
|---|---|
| CPU | AMD Ryzen 7 7800X3D, 8 çekirdek / 16 thread |
| TSC hızı | 4,2000 GHz (kalibrasyonla ölçüldü) |
| CPU pinleme | `taskset -c 4`, koşunun tamamı tek çekirdekte |
| İşletim sistemi | AlmaLinux 8.10, WSL2 üzerinde, kernel 6.18.40.1-microsoft-standard-WSL2 |
| Derleyici | GCC 8.5.0 |
| Flag'ler | `-std=c++11 -O3 -DNDEBUG -Wall -Wextra -Wpedantic` |
| Zamanlama, bütün pass | `std::chrono::steady_clock` |
| Zamanlama, datagram başına | `rdtsc`, cycle başına 0,238 ns |
| Warm-up | ilk ölçümden önce ölçülmeyen bir tam pass |
| İterasyon | 50 |

**Bu bir sanal makine.** WSL2 bir Hyper-V hypervisor'ı altında koşuyor, yani Windows
host misafiri komple durdurabiliyor — pinleme buna çare değil. Bu, medyana ve işe yarar
percentile'lara dokunmuyor ama maksimum tamamen ona ait. Latency bölümündeki nota bak.

## Yöntem

1. Input dosyası, hiçbir şey ölçülmeden önce belleğe okunuyor. Okumayı ölçmek WSL ile
   host arasındaki 9p dosya sistemini ölçmek olurdu — o yaklaşık 2,9 saniye sürüyor,
   asıl işse 0,10 saniye.
2. Bir warm-up pass'ı ölçülmeden koşup atılıyor. Soğuk bir ilk pass, yeni allocate
   edilmiş 584 MB'lık sayfayı tek tek page fault'la içeri alıyor ve cache'ler ile branch
   predictor'lar boş başlıyor; bunların hiçbiri kodun davranışı değil.
3. Ardından elli ölçülen pass geliyor. Her biri yeni bir pipeline kuruyor, yani her pass
   boş bir book'la başlıyor ve birebir aynı işi yapıyor.
4. Timed region'ın içinde hiç dosya ya da konsol çıktısı yok.
5. Her pass bir checksum üretiyor. Programdan çıkan bir değer olmasaydı `-O3` bütün
   hesabı ölü sayıp silmekte haklı olurdu.

Allocation sayacı pipeline kurulduktan sonra sıfırlanıyor ve pass bittikten sonra
okunuyor, yani kurulumun değil hot path'in ne istediğini raporluyor.

### İki latency rakamı neyi ölçüyor

**Mesaj başına ortalama ns**, bütün pass'ın süresinin mesaj sayısına bölünmesinden
geliyor. İçinde mesaj başına hiçbir ölçüm aleti yok.

**Datagram başına percentile'lar**, her sekizinci datagram'da `process_datagram`
çağrısının ölçülmesinden geliyor. O çağrı, ödevin tarif ettiği arayüzün kendisi; ve birim
burada anlamlı çünkü feed neredeyse mükemmel biçimde tek tip: 519.492 packet'in
510.703'ü tam olarak 30 mesaj taşıyor, hiçbiri 30'u geçmiyor.

Her datagram'ı ölçmekten bilerek kaçındım. `steady_clock`'un maliyeti onlarca nanosaniye
ve medyan datagram 119 ns — yani ölçü aleti ölçümün büyük bir parçası olurdu. `rdtsc`
0,238 ns ile zaten yeterince ucuz, ama sekizde bir örnekleme onu tamamen yoldan
çıkarıyor.

Örneklenen bölge sadece `process_datagram`. Packet sınırını bulmak dışarıda, çünkü canlı
bir feed onu UDP'den bedavaya alıyor ve sadece dosyanın onu geri kurması gerekiyor.
Datagram başına throughput rakamının (195 ns) örneklenmiş ortalamadan (154 ns) yüksek
olmasının sebebi bu.

## Sonuçlar

| Kategori | Metrik | Değer |
|---|---|---|
| Hacim | Packet | 519.492 |
| | ITCH mesajı | 15.560.041 |
| | Target mesajı | 308.209 |
| | Order intent | 1.491 |
| Throughput | Toplam süre, 50 pass | 5,277 s |
| | En iyi pass | 0,101 s |
| | En kötü pass | 0,144 s |
| | Saniyede mesaj | 154,4 M |
| | Mesaj başına ortalama ns | 6,5 |
| Latency, datagram başına | p50 | 119 ns |
| | p90 | 209 ns |
| | p99 | 459 ns |
| | p99.9 | 1.039 ns |
| | Maksimum | 8.306.725 ns |
| | Ortalama | 154 ns |
| | Örnek sayısı | 3.246.800 |
| Kaynak | Hot path allocation | **0** |
| | Hot path byte | **0** |
| | Tepe bellek | 577.420 kB |
| Doğrulama | Checksum | `0x45F146A704E0878C` |
| | 50 pass'ın hepsinde sabit | evet |

Mesaj başına, datagram rakamlarını 30'a bölersek: medyanda yaklaşık 4 ns, p99.9'da
yaklaşık 35 ns.

## Sayılar nasıl okunmalı

**Sıfır allocation iddia değil, ölçüm.** Global `operator new` değiştirildiği için hem
programdaki hem de linklenen her şeydeki her allocation sayılıyor. Kurulumdan sonra
sayaç sıfırlanıyor; pass bittiğinde sıfır okuyor. Hot path'te hiçbir şey allocator'dan
bir şey istemiyor.

**Maksimum kodun değil, hypervisor'ın.** Tek bir datagram için 8,3 ms, p99.9'un dört
büyüklük mertebesi ötesinde; ve packet'ler tek tip olduğu için suçlanacak büyük bir
datagram yok. Olan şey, misafirin komple durdurulması. 3.246.800 örneğin 978'i 4 µs'yi
aşıyor, yani %0,03. Bare metal'de p99.9 ötesindeki kuyruğu ciddiye almak gerekirdi;
burada o kuyruk çoğunlukla odanın bir özelliği.

**Asıl önemli sayı p99.9'un p50'ye oranı.** 119 ns'ye karşı 1.039 ns, yani kabaca dokuz
kat — ve tasarımın görüldüğü yer burası. Occupancy bitset'i, bir level üstünde boşlukla
birlikte boşaldığında best-price aramasının bir scan'e dönüşmesini engelliyor;
backward-shift deletion ise kaç order gelip giderse gitsin hash probe'larını kısa
tutuyor. İkisi de ortalama için değil, kuyruk için seçildi.

**Throughput'u belirleyen şey atılan mesajlar.** 15,56 M mesaj geliyor ve 308 bini hedef
book'a ait, yani %98'i type byte'ı okunup, uzunluk kontrol edilip, tek bir 32-bit field
karşılaştırılarak eleniyor. Mesaj başına ortalama 6,5 ns'de zamanın çoğunda ölçülen şey
bu eleme yolu. Hedef bir mesajın işi 6,5 ns'den epey fazla; ortalamanın düşük olmasının
sebebi mesajların çoğunun oraya hiç ulaşmaması.

**Tepe bellek, input dosyasının kendisi.** 557 MiB'lık dosyaya karşı 577 MB resident.
Order pool, price level'lar ve journal toplamı birkaç megabyte: 24 byte'lık 65.536 order
slot'u (1,5 MB), iki index table'ı (toplam 1 MB), taraf başına 16 byte'lık 16.384 level
(toplam 512 KB), ve journal'ın üç dizisi.

## Burada ölçülmeyenler

**Mesaj başına latency dağılımı.** Bunu elde etmek her mesajın etrafına bir timer koymak
demekti, ve mesaj başına yaklaşık 4 ns'lik bir işte timer ölçtüğü şeyden pahalıya
gelirdi. Datagram başına dağılım artı tek tip packet boyutu, buna en yakın dürüst
karşılık.

**Sequence gap içeren bir feed'deki davranış.** Bu dosyada hiç gap yok, yani reliability
yolu hiç tetiklenmiyor ve burada hiçbir maliyeti yok.

**Bare metal bir koşu.** Yukarıdaki her şey bir VM'in içinden geliyor, ve kuyruk dışarıda
farklı görünürdü.

---

*Bu rapor elle yazıldı. İçindeki bütün sayılar programın `--benchmark` modunun stdout
çıktısından alındı; program bu dosyayı kendisi üretmiyor. Benchmark başka bir makinede
ya da başka parametrelerle koşulursa buradaki rakamların elle güncellenmesi gerekir.*
