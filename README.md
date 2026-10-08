# Low Latency Order Book and Strategy

MoldUDP64 / BISTECH ITCH market data replay, order book reconstruction, tek tick'lik
gap stratejisi ve P&L simülasyonu.

## Derleme

```
make          # release, -O3 -DNDEBUG
make debug    # -O0 -g, invariant kontrolleri açık
make check    # testleri derleyip çalıştır
```

Hedef toolchain: GCC 8.5.0, C++11, Linux x86-64. Hiçbir external dependency yok.

## Çalıştırma

```
./orderbook --input sample.bin --config config.txt [--orders orders.csv]
taskset -c 4 ./orderbook --benchmark sample.bin --iterations 50 --config config.txt
```

Birincisi özeti stdout'a basıyor ve order'ları CSV'ye yazıyor. İkincisi data'yı tekrar
oynatıp throughput, latency, allocation ve checksum raporluyor. Benchmark'ı tek
çekirdeğe pinle: pinlemezsen scheduler thread'i çekirdekler arasında gezdiriyor ve
kuyruk, kodla hiç alakası olmayan soğuk cache'leri toplamaya başlıyor.

## Örnek veride sonuçlar

`itch_data_250815_HI2.dat`, verilen config ile. 584 MB, yaklaşık üç saniye:

```
final_position          = 300
cash_flow               = -6157000
final_pnl               = -409000
last_execution_price    = 19150
end_of_day_mark_price   = 19160
generated_order_count   = 1491          (747 buy, 744 sell)
checksum                = 0x45F146A704E0878C
```

P&L tutuyor: `-6.157.000 + 300 × 19.160 = -409.000`. Mark fiyatı kapanıştaki fiyat,
dosyadaki son fiyat değil — ikisini birden basmamın sebebi bu.

197.463 evaluation nereye gitti:

| Sonuç | Adet |
|---|---|
| spread iki tick değildi | 179.951 |
| gap'i o group açmamıştı | 15.532 |
| position limiti yer bırakmadı | 380 |
| continuous trading dışındaydı | 109 |
| **order gönderildi** | **1.491** |

İki kez çalıştırınca özet de CSV de byte byte aynı çıkıyor.

**Strateji bu günde para kaybediyor.** Bu bir bug değil, fill model'in doğal sonucu:
üretilen her order fill olmuş sayılıyor, oysa gerçek piyasada fill olan order'lar
ağırlıklı olarak keşke olmasaydı dediklerin oluyor. Known limitations bölümüne bak.

## Mimari

Data tek yönde akıyor. Her adım tek bir iş yapıp sonucu bir sonrakine veriyor.

```
input dosyası
   -> DatagramSource    dosyayı MoldUDP64 packet'lerine böl
   -> MoldSession       header'ı kontrol et, sequence'i takip et, block'ları çıkar
   -> ItchParser        message type'ı oku, saati tut, diğer book'ları ele
   -> OrderBook         add / execute / delete uygula, best bid ve ask'i takip et
   -> Engine            timestamp group'unu kapat, sonra stratejiyi bir kez çalıştır
   -> GapStrategy       kurallar izin veriyorsa bir order üret
   -> FillSimulator     o order'ı fill'e çevir
   -> Position          cash flow, position, gün sonu P&L
```

Her adım bir sonrakini base class üzerinden değil, bir template parametresi üzerinden
tanıyor. Çağrı derleme zamanında çözülüyor, yani hot path'te virtual dispatch yok, ve
testler gerçek adımın yerine küçük bir recorder koyabiliyor.

| Bileşen | Neden sorumlu |
|---|---|
| `core/types.hpp` | herkesin paylaştığı scalar type'lar ve enum'lar |
| `core/byte_reader.hpp` | byte buffer'dan güvenli okuma: sınır, big-endian, alignment varsayımı yok |
| `core/config.hpp` · `src/config.cpp` | yedi parametre, açılışta bir kez, eksik key hata |
| `io/datagram_source.hpp` | dosyada packet sınırını bulma |
| `protocol/itch_messages.hpp` | field offset'leri, message uzunlukları, küçük struct'lara decode |
| `protocol/mold_udp64.hpp` | packet framing, session takibi, sequence state machine |
| `protocol/itch_parser.hpp` | type dispatch, `'T'` saati, target book filtresi |
| `book/order_pool.hpp` | sabit kapasiteli order deposu + order id index'i |
| `book/price_levels.hpp` | price başına toplamlar, best bid/ask, occupancy bitset'i |
| `book/group_journal.hpp` | her level'ın group başındaki hali |
| `book/order_book.hpp` | dört ITCH message'ının ne anlama geldiği |
| `strategy/order_intent.hpp` | stratejinin çıktı tipi ve sink kavramı |
| `strategy/gap_strategy.hpp` | yedi kural, position limiti, skip sayaçları |
| `sim/fill_simulator.hpp` | order intent → fill (değiştirilebilir olsun diye ayrı) |
| `sim/position.hpp` | position, cash flow, gün sonu mark to market |
| `app/engine.hpp` | timestamp grouping ve stratejinin ne zaman çalışacağı |
| `app/pipeline.hpp` | bütün zinciri kurup bir pass koşturan sınıf |
| `app/order_log.hpp` · `app/stats.hpp` | üretilen order'lar ve FNV-1a checksum |
| `app/latency.hpp` · `app/alloc_counter.hpp` | histogram + percentile, allocation sayacı |
| `app/benchmark.hpp` | warm-up, N pass replay, determinizm doğrulaması |
| `app/reporter.hpp` · `src/reporter.cpp` | deterministik özet ve orders.csv |
| `src/main.cpp` | CLI, dosyayı belleğe alma, mod seçimi |

## Veri yapıları ve nedenleri

**`ByteReader` bir view, kopya değil.** İçinde bir pointer ve bir uzunluk var.
`slice()` aynı bellek üzerinde daha küçük bir view açıyor, yani bir packet'in içinden
bir message çıkarmanın maliyeti sıfır. Input dosyası bir kez belleğe alınıyor ve bütün
reader'lar onun içine bakıyor.

**Okumalar byte byte yapılıyor.** `*(uint32_t*)(p + 13)` gibi bir cast üç ayrı sebepten
yanlış olurdu: dosya big-endian, x86 little-endian; offset 13 dört byte'lık bir okuma
için hizalı değil; ve cast, byte'ların gerçekten orada olup olmadığını kontrol etmiyor.
Byte byte okuyup kaydırmak üçünü birden çözüyor. `-O3` döngüyü tek bir `bswap`'a geri
katladığı için okunabilir kod bedavaya geliyor.

**Sınır kontrolü field başına değil, message başına yapılıyor.** İşlenen her message'ın
sabit bir uzunluğu var, yani tek bir `has(45)` çağrısı bir Add Order'ın on bir field'ının
da buffer'ın içinde olduğunu kanıtlıyor. On bir karşılaştırma yerine bir tane. Field
okuyucularında emniyet olarak birer `assert` duruyor, onlar da `-DNDEBUG` ile yok oluyor.

**Handler'lar template parametresi, virtual interface değil.** Virtual bir çağrı message
başına bir table lookup demek, ve derleyicinin inline etmesini engelliyor. Template ile
derleyici her handler tipi için bir kopya üretiyor ve çağrının içini görebiliyor. Aynı
tercih protocol katmanını, arkasında bir order book olmadan test edilebilir yapıyor.

**Hot path header'larda yaşıyor.** Link-time optimisation olmadan GCC `.cpp` dosyaları
arasında inline edemiyor. Parsing'i, book'u ve stratejiyi header'da tutmak, bir
message'dan bir strateji kararına kadar bütün zinciri inline etmesine imkân veriyor.
Sadece soğuk kod — config yükleme, raporlama, benchmark harness'ı, `main` — `src/`'de.

**Order'lar sabit bir pool'da duruyor, hash index'inden bulunuyor.** Pool,
`max_active_orders` kadar slot'tan oluşan tek bir dizi. Boş slot'lar bir linked list
oluşturuyor ve "next" pointer'ı kullanılmayan slot'un kendi içinde duruyor, yani slot
almak da geri vermek de O(1) ve ek bellek istemiyor. Ayrı bir index, order id'yi slot
numarasına eşliyor.

Akla ilk gelen alternatif `std::unordered_map`'ti ve üç sebepten elendi: order başına bir
node allocate ediyor, ki bu message başına allocation demek; node'lar allocator nereye
koyduysa orada duruyor, yani her lookup soğuk belleğe bir pointer takibi; ve büyürken
rehash yapıyor, ki bu feed'in ortasında öngörülemez bir duraklama.

Index, 2'nin kuvveti boyutunda ve en fazla yarısı dolu bir table üzerinde open addressing
+ linear probing. Ortalama lookup bir ya da iki bucket'a dokunuyor. Silme işlemi
tombstone bırakmak yerine entry'leri geri kaydırıyor. Tombstone daha basit olurdu ama
burada order'lar sürekli siliniyor ve o mezarlık zamanla her probe'u bir scan'e
çevirirdi.

**Side başına bir index var, ama pool ortak.** Bir order hangi side'da olursa olsun bir
slot kaplıyor, yani pool paylaşılıyor ve toplam tek bir sayıyla sınırlanıyor. Index'in
side başına olması zorunlu, çünkü ITCH order id'leri book *ve side* bazında unique: aynı
id aynı anda iki side'da da canlı olabiliyor, ve tek bir table ikincisine duplicate
derdi.

**Price level'lar `price - base_price` ile index'lenen yoğun bir dizi.** Fiyatlar tam
kuruş, yani level'a tek bir çıkarmayla ulaşılıyor. Arama yok, karşılaştırma yok, hiçbir
şey yeniden sıralanmıyor. Sıralı bir vector best price'ı bedavaya index 0'a koyardı, ama
baskın işlem order id → price → level ve o her seferinde bir binary search olurdu; dahası
insert ve erase index'leri kaydırdığı için bir order kendi level'ının nerede olduğunu
tutamazdı.

Dizi kuruş başına bir slot tutuyor. Enstrüman 10 kuruşluk bir grid'de işlem gördüğü için
slot'ların onda dokuzu boş kalıyor. Bunu kabul ettim: dizi zaten küçük (taraf başına
16.384 slot, toplam yarım megabyte civarı, yani cache'te kalıyor) ve index hesabı tek bir
çıkarma olarak kalıyor. Tick'e bölmek bir division eklerdi ve `tick_size`'ın gerçekten
sabit olmasına bel bağlardı.

`base_price` görülen ilk fiyattan sabitleniyor, band'ın ortasına oturtularak. Band dışında
bir fiyat, dizinin dışına yazmak yerine güvenli bir hata.

**İki side ayrı dizilerde.** Level başına bir side tag'i tutan tek dizi belleği yarıya
indirirdi, ama bir timestamp group'unun içinde bir fiyat gerçekten aynı anda iki side'da
olabiliyor: yeni ask'i eklemeden önce eski bid'i silmemek geçerli bir olay sırası, ve tek
bir tag bunu temsil edemiyor.

**Best bid ve ask artımlı hareket ediyor.** Yeni bir level best'i sadece iyileştirebilir,
yani eklemek tek bir karşılaştırma. Best level boşaldığında bir sonraki, level başına bir
bit tutan bir bitset üzerinden bulunuyor: tek bir 64-bit word 64 level'ı birden
cevaplıyor, ve `__builtin_ctzll` ilgili biti tek instruction'da çekip çıkarıyor.
Level'ları tek tek yürümek çoğu zaman sorunsuz olurdu, arada bir ise felaket — ki bu tam
olarak tail latency'yi mahveden şekil.

## Performans

584 MB'lık feed üzerinde elli pass, tek çekirdeğe pinli. Bütün yöntem ve ortam
[benchmark_report.md](benchmark_report.md) içinde.

| | |
|---|---|
| En iyi pass | 15.560.041 message için 0,101 s |
| Throughput | 154 M message/saniye, message başına 6,5 ns |
| Datagram başına latency | p50 119 ns, p99 459 ns, p99.9 1.039 ns |
| **Hot path allocation** | **0** |
| Tepe bellek | 577 MB, bunun 557 MiB'ı input dosyası |
| Checksum | elli pass'ın hepsinde aynı |

Sıfır allocation bir iddia değil, bir ölçüm: global `operator new` değiştirildiği için
programın herhangi bir yerindeki her allocation sayılıyor. Sayaç pipeline kurulduktan
sonra sıfırlanıyor ve pass bittiğinde okunuyor.

Throughput'un büyük kısmı aslında atılan message'lar. Feed'in %98'i başka order book'lara
ait ve type byte'ı okunup, uzunluk kontrol edilip, tek bir 32-bit field karşılaştırılarak
eleniyor — yani 6,5 ns'lik ortalama çoğunlukla "hayır" demenin maliyeti. Hedef bir
message'ın maliyeti bundan epey yüksek.

p50 ile p99.9 arasındaki fark, tasarımın görüldüğü yer. Occupancy bitset'i, bir level
üstünde boşlukla birlikte boşaldığında best-price aramasının bir scan'e dönüşmesini
engelliyor; backward-shift deletion ise kaç order gelip giderse gitsin hash probe'larını
kısa tutuyor. İkisi de ortalama için değil, kuyruk için seçildi.

Raporlanan maksimum olan 8,3 ms kodun değil, hypervisor'ın: bu makine WSL2 altında
koşuyor ve host misafiri komple durdurabiliyor. Packet'ler tek tip, her biri 30 message,
yani suçlanacak büyük bir datagram yok.

## Karmaşıklık

| Adım | Zaman | Yer |
|---|---|---|
| Bir field oku | O(1) | yok |
| Bir packet'i çerçevele | O(block sayısı) | yok |
| Bir message'ı dispatch et | O(1) | stack'te küçük bir struct |
| Hedef dışı bir message'ı ele | O(1), iki okuma | yok |

Protocol katmanında hiçbir şey scan etmiyor ya da sıralamıyor. Bir packet, block'ları
üzerinden tek bir geçişe mal oluyor; bir message ise sabit sayıda okumaya.

Order book ve üstündeki katmanlar:

| Adım | Zaman | Yer |
|---|---|---|
| Order ekle | beklenen O(1) | 24 byte'lık bir slot |
| Order'ı id'den bul | beklenen O(1) | yok |
| Order sil | beklenen O(1), artı geri kaydırdığı probe dizisi | yok |
| Bir level'da miktar ekle/çıkar | O(1) | yok |
| Best price, level dolu kalıyorsa | O(1) | yok |
| Best price, level boşaldıktan sonra | bitset'in O(boşluk / 64) word'ü | yok |
| Bir level'ı group'ta dokunulmuş işaretle | O(1) | yok |
| Yeni group başlat | O(1) | yok |
| Bir level için stratejiyi değerlendir | O(1) | yok |
| Bir fill kaydet | O(1) | yok |

Hash işlemlerindeki "beklenen", probe dizisinin kısa olmasına dayanıyor, o da table'ın
seyrek kalmasına. Table `max_active_orders`'ın iki katı boyutunda, yani %50 doluluğu hiç
geçmiyor; ve silme, tombstone bırakmak yerine diziyi geri kaydırdığı için bir seansta kaç
order gelip giderse gitsin probe'lar uzamıyor.

Sabit olmayan tek adım, bir level boşaldıktan sonra bir sonraki best price'ı bulmak.
Occupancy bitset'i bunu word başına 64 level'a indiriyor, yani 1.000 kuruşluk bir boşluk
1.000 kontrol değil 16 word. Alternatifi sıralı bir container'dı; o da bu tek adımı
ucuzlatmak için her insert'ü O(log n) yapardı. Boşluk nadir, insert'ler değil.

Group başlatmanın O(1) olmasının sebebi bir generation counter: journal level başına bir
generation numarası tutuyor ve dizilerini temizlemek yerine tek bir sayacı artırıyor.
Generation'ı eski kalan bir level "dokunulmamış" okunuyor. Her timestamp değişiminde
taraf başına 16.384 entry'yi temizlemek, engine'in yaptığı tek başına en pahalı iş
olurdu.

## Allocation, kapasite ve hata yönetimi

Input dosyası herhangi bir timed region'dan önce bir kez belleğe alınıyor. Ondan sonra
protocol katmanı hiç allocate etmiyor: `ByteReader` sadece bir pointer ve bir uzunluk
tutuyor, decode edilen message'lar stack'te düz struct'lar, ve sayaçlar sabit field'lar.

Order pool allocate ediyor, ama sadece constructor'ında. `max_active_orders` kadar slot
ve bunun iki katı boyutunda bir index table alıyor; ikisi de bir kez boyutlanıyor ve asla
büyümüyor. İkisi için de `std::vector` kullanılıyor çünkü belleği güvenli şekilde
sahipleniyor, ama hiçbirine `push_back` çağrılmıyor — yani hiçbir şey yer değiştirmiyor
ve pool'a bakan hiçbir pointer bayatlamıyor. Ödevin yasakladığı şey message başına
allocation, kurulum değil.

Hatalar return value ve sayaç, asla exception değil. Ödev exception'ın hot path'te normal
kontrol akışı olarak kullanılmamasını istiyor, ve burada bozuk bir packet zaten normal
bir durum: sayılıyor ve atlanıyor, ortalıkta fırlatılmıyor.

Her problem türü ayrı ayrı sayılıyor, böylece son rapor gerçekte ne olduğunu
söyleyebiliyor:

| Sayaç | Ne zaman artıyor |
|---|---|
| `invalid_packets` | header 20 byte'tan kısa, block length packet'in dışına taşıyor, ya da block'lar datagram'ı tam doldurmuyor |
| `sequence_gaps` | bir packet ya da heartbeat beklenen message'tan ileriden başlıyor |
| `duplicates` | tamamı ya da bir kısmı daha önce görülmüş bir packet |
| `invalid_messages` | işlenen bir type yanlış uzunlukta, ya da side byte'ı `B` veya `S` değil |
| `skipped_types` | bu build'in işlemediği bir message type'ı |

Bozuk bir side byte'ı order book'ta değil, parser'da reddediliyor. `Side` aşağıda dizi
index'i olarak kullanılıyor, yani bozuk bir değerin güvenle durdurulabileceği son nokta
orası.

İki limite ulaşılabiliyor, ve ikisi de bir şeyi bozmak yerine stratejiyi durduruyor:

- `max_active_orders` — pool dolduğunu bildiriyor ve order saklanmıyor. Hiçbir şeyin
  üzerine yazılmıyor, hâlihazırdaki order'lara dokunulmuyor.
- `max_price_levels` — band dışındaki bir fiyat reddediliyor. Order pool'a girmişse geri
  çıkarılıyor, yani pool ile level'lar hiçbir zaman birbirine ters düşmüyor.

İkisi de stratejinin kontrol ettiği bir bayrağı kalıcı olarak indiriyor. O noktadan sonra
book'a güvenilemez, o yüzden yanlış olduğunu bildiği bir book üzerinden işlem yapmak
yerine order üretmeyi kesiyor.

## Sequence gap davranışı

MoldUDP64 packet değil, **message** numaralıyor. Bir packet header'ı içindeki ilk
message'ın sequence numarasını ve kaç message taşıdığını veriyor, yani beklenen sonraki
numara `sequence + count`.

Dört durum ayırt ediliyor. Diyelim beklenen sonraki message 8:

| Packet ne diyor | Durum | Ne oluyor |
|---|---|---|
| ilk 8, count 3 | sırada | üçü de işleniyor |
| ilk 12, count 1 | **gap** | 8–11 kayıp; gap sayılıyor, book unreliable işaretleniyor, packet yine de işleniyor |
| ilk 4, count 3 | **zaten görülmüş** | packet komple atılıyor |
| ilk 6, count 4 | **kısmi örtüşme** | 6 ve 7 atlanıyor, 8 ve 9 işleniyor |

Son durum önemli. Bir retransmission elimizdekiyle örtüşebiliyor. Hepsini işlemek aynı
order'ları iki kez uygulayıp book'u bozardı. Hepsini atmak ise yeni message'ları
kaybettirirdi.

Heartbeat'ler bir sonraki message'ın sequence numarasını taşıyor, yani hiç data
taşımadıkları halde bir gap'i gösterebiliyorlar. O durum da ele alınıyor.

**Bir gap'ten sonra book unreliable işaretli kalıyor.** Bu bayrak inikken strateji yeni
order üretmiyor. Bayrak ancak session değiştiğinde geri kalkıyor, çünkü yeni bir session
yeni bir stream demek ve eski gap artık geçerli değil.

Bu bir karar, ödevin söylediği bir şey değil. Kaçırılan message'lar GLIMPSE gibi bir
snapshot feed'i olmadan geri alınamıyor, o yüzden ihtiyatlı okuma şu: aynı session'ın
içinde book bir daha tam olarak toparlanmıyor.

## Varsayımlar

Protokol dokümanlarının ya da ödevin açık bıraktığı noktalar bunlar.

**`'T'` Seconds message'ları takip ediliyor ama book'a hiç uygulanmıyor.** ITCH
timestamp'i ikiye bölüyor: saniye kendi message'ında geliyor, diğer her message ise o
message'tan beri geçen nanosaniyeyi taşıyor. Yani nanosaniye alanı her saniye baştan
başlıyor. Sadece ona bakarak gruplamak, bir saniye arayla gelen iki message'ı tek bir
group'ta birleştirirdi. Parser bunun yerine mevcut saniyeyi tutup 64-bit bir değer
kuruyor: `saniye × 1.000.000.000 + nanos`. Ödev işlenecek dört message type'ı sayıyor ve
`'T'` onlardan biri değil, ama bir message'ı izlemekle onu book'a uygulamak farklı iki
şey.

**İlk `'T'` gelmeden önceki message'lar saniye 0 ile işleniyor.** Bu sadece kayıt
stream'in ortasından başlarsa oluyor. O durumda mutlak zaman yanlış, ama o aralıktaki
sıralama yine doğru çalışıyor, yani message'ları atmak kazandırdığından fazlasını
kaybettirirdi. `before_first_second` sayacında tutuluyorlar.

**`'C'` ve `'Y'` atlanıyor, ve bu feed'de bunun bir bedeli yok.** Ödev sadece dört message
type'ının işlenmesini söylüyor, ama spec'in kendi "How to Build an Order Book View"
bölümü `'C'` (Order Executed with Price — bir order'ın quantity'sini azaltıyor) ve `'Y'`
(Order Book Flush — bir book'u komple temizliyor) type'larını da sayıyor. İkisi de örnek
veriye karşı kontrol edildi:

- `'C'` hiç geçmiyor. 15,5 milyon message içinde bir kez bile.
- `'Y'` 3.897 kez geçiyor ve book 73616'ya dört kez dokunuyor. Hiçbiri sonucu
  değiştiremez. Birincisi book daha boşken geliyor. İkincisi `P_MARJ_YAYIN_KAPANIS` ile
  aynı timestamp'i paylaşıyor, yani strateji o noktada zaten durmuş. Diğer ikisi
  kapanıştan sonra.

Yani burada atlamak güvenli. Genel olarak güvenli değil, ve `'C'` kullanan ya da
continuous trading sırasında flush gelen bir feed ikisini de gerektirirdi.

`'U'` (Order Replace) BIST piyasalarında kullanılmıyor, yani onun da bir bedeli yok.

**Session değişimi sequence takibini sıfırlıyor ve güveni geri veriyor.** Farklı bir
session adı, kendi numaralandırması olan farklı bir stream demek; yani değişimden sonraki
düşük bir sequence numarası duplicate değil, ve eski stream'den kalan bir gap artık
geçerli değil.

**`B` ya da `S` olmayan bir side byte'ı message'ı geçersiz yapıyor.** Message tahmin
edilmek yerine sayılıp atılıyor.

**`tick_size` 10, ödevin gösterdiği 1 değil.** Ödevin örnek config'i 1 diyor, ve o değerle
hiçbir spread tam olarak iki tick olmuyor, yani strateji hiç tetiklenmiyor. Book 73616, 10
kuruşluk bir grid'de işlem görüyor: feed'deki her komşu fiyat çifti 10 kuruş arayla, ve
spread continuous trading'in %90,8'inde 10, %8,6'sında 20. O 20 zaten aradığımız tek boş
adım. Veriyle birlikte gelen config sonradan 10'u doğruladı.

**`minimum_position` 0, yani short satış yok.** Satış miktarı
`position - minimum_position`, ve taban sıfır olunca sadece elde olan satılabiliyor.
Dolayısıyla günün ilk order'ı her zaman bir alış. Bir yan etkisi var: her order tam 100
lot çıkıyor, çünkü fill'ler pozisyonu hep 100'lük adımlarla hareket ettiriyor, yani kalan
yer ya sıfır ya da en az 100 oluyor ve kısmi boyutlandırma hiç devreye girmiyor.

**Kapasitenin `-1` olması o limiti kapatıyor, ama yine de bir şeyin allocate edilmesi
gerekiyor.** Fixed-capacity bir tasarımın "kapasitesiz" olması mümkün değil, o yüzden -1
yerleşik bir default seçiyor: 65.536 order ve 16.384 price level. Feed'in target book'u en
fazla 4.528 canlı order ve 3.780 kuruşluk bir band gördü, yani ikisi de gerekenin on
katından fazla yer bırakıyor. Yapıları büyütmek alternatifti ve reddedildi: allocation'ı
hot path'e geri koyardı, ki ödevin yasakladığı tek şey o. Default'u aşmak yine güvenli bir
hata, çünkü dürüst bir alternatifi yok — ama artık "konfigüre edilen limite ulaşıldı"
demek değil.

**Gün `P_MARJ_YAYIN_KAPANIS` geldiğinde değerleniyor, dosya sonunda değil.** Feed kapanıştan
sonra da işlem görmeye devam ediyor — kapanış seansı, yeni execution'lar — ve son işlem
fiyatı kayıyor. Bu günde kapanışta 19.160, dosyanın sonunda 19.150.

**Price, spec'teki sentinel'i kullanıyor.** 31. biti set ve diğer her biti sıfır olan bir
Price field'ı (`INT32_MIN`) "price yok" demek, ve bir Add Order'da market order anlamına
geliyor. Böyle bir order'ın price level'ı yok ve bir level'a konmamalı.

**Dosyadaki packet framing'i arka arkaya, doğrulandı.** Bu konuda hiçbir yerde bir şey
yazmıyor, o yüzden doğrudan kontrol ettim: dosyada pcap ya da pcapng magic number'ı yok,
ve onu çıplak MoldUDP64 packet'leri olarak yürümek — 20 byte header artı block'lar
üzerinden `2 + block length` toplamı — 519.492 packet sonra tam olarak son byte'a
oturuyor, artan yok ve sonu taşan bir block yok. Yine de `io/datagram_source.hpp`'nin
arkasında tutuluyor, böylece başka bir düzene sahip bir feed tek dosyalık bir değişiklik
oluyor.

Cash flow ve P&L `int64_t`'de tutuluyor. Fill başına en büyük terim
`order_quantity * price`, yani konfigüre edilen değerlerle (100 lot, üst sınır 100.000
kuruş) bu 10⁷ ediyor ve `INT64_MAX`'a kadar yaklaşık 9×10¹¹ fill'lik yer kalıyor — tipik
bir seansın ürettiği order sayısının yaklaşık 4 milyar katı. Konfigürasyon daha uç
olursa (10⁶ lot, `INT32_MAX`'a yakın bir fiyat) bu pay yaklaşık 4.300 fill'e düşüyor, o
yüzden hem çarpma hem toplama debug build'lerinde `__builtin_mul_overflow` ve
`__builtin_add_overflow` ile kontrol ediliyor. `__int128` kullanılmamasının sebebi basit:
ISO C++11'in dışında kalıyor ve `-Wpedantic` altında uyarı veriyor.

## Benchmark

```bash
taskset -c 4 ./orderbook --benchmark itch_data_250815_HI2.dat --iterations 50 --config config.txt
```

Sayının ne anlama geldiğini beş kural belirliyor:

1. Dosya, hiçbir şey ölçülmeden önce bellekte.
2. Bir warm-up pass'ı ölçülmeden koşup atılıyor.
3. Data çok kez oynatılıyor, ve her pass yeni bir pipeline kuruyor.
4. Timed region'ın içinde hiç dosya ya da konsol çıktısı yok.
5. Her pass bir checksum üretiyor, böylece `-O3` işi ölü sayıp silemiyor.

Allocation sayacı pipeline kurulduktan sonra sıfırlanıyor ve pass bittiğinde okunuyor,
yani kurulumun değil, hot path'in ne istediğini raporluyor.

Bütün ortam, yöntem ve sonuçlar: [benchmark_report.md](benchmark_report.md).

## Testler

```
make check
```

Çalışmaya başladığımda örnek piyasa verisi henüz elimde değildi, o yüzden test
yardımcıları sentetik input üretiyor. `tests/message_builder.hpp` gerçek MoldUDP64
packet'leri ve dört ITCH message'ı yazıyor, ve her seferinde tek bir şeyi bozabiliyor:
header'ı kısa kesmek, packet'in dışına taşan bir block length koymak, ya da packet'in
taşıdığından fazla message iddia etmek. Protocol testleri bunun üzerine kurulu.

Wire formatının iki tarafı birlikte test ediliyor. Builder byte yazıyor, `ByteReader`
geri okuyor, ve bir round-trip testi her field'ı karşılaştırıyor. Ayrı ayrı yazılsalardı
ikisi yanlış bir offset üzerinde anlaşıp tek tek bakıldığında doğru görünebilirdi; bunu
sadece round trip yakalıyor.

216 test, iki katman halinde. Çoğu tek bir bileşeni kendi arayüzüne karşı kontrol ediyor.
Onların üstünde `test_protocol.cpp`, `test_strategy.cpp` ve `test_pnl.cpp` wire byte'ından
bir feed kurup bütün programın içinden geçiriyor ve sonucu raporlanan özetten okuyor. Bir
bileşen kendi başına doğru olup yine de yanlış bağlanmış olabilir: yanlış nesneden okunan
bir sayaç, hiç ulaşılmayan bir handler, yanlış sayıya uygulanan bir limit. Bunu sadece
bütün zincirden geçen bir koşu yakalıyor.

Ödevin istediği senaryolar ve her birinin uçtan uca nerede kontrol edildiği:

| Senaryo | Test |
|---|---|
| Kesik header | `protocol_a_truncated_header_stops_the_walk_and_is_reported` |
| Geçersiz message length | `protocol_a_message_shorter_than_its_type_is_counted_and_skipped` |
| Sequence gap | `protocol_a_sequence_gap_is_counted_and_silences_the_strategy` |
| Heartbeat | `protocol_a_heartbeat_carries_nothing_and_breaks_nothing` |
| End of session | `protocol_end_of_session_is_a_bare_header_and_frames_correctly` |
| Hedef dışı order book | `protocol_another_order_book_never_reaches_the_engine` |
| Tek group içinde açılıp kapanan gap | `strategy_e2e_a_gap_that_opens_and_closes_in_one_group_is_not_a_signal` |
| Gerçek BUY gap'i | `strategy_e2e_a_bid_that_vanishes_produces_one_buy` |
| Gerçek SELL gap'i | `strategy_e2e_an_ask_that_vanishes_produces_one_sell` |
| Tekrar eden signal bastırılıyor | `strategy_e2e_one_order_per_gap_however_long_it_stays_open` |
| Limite yakın quantity kısılması | `pnl_e2e_the_quantity_is_cut_down_to_what_the_limit_allows` |
| Limitte order üretilmemesi | `pnl_e2e_nothing_is_sent_when_there_is_no_room_left` |
| BUY cash flow | `pnl_e2e_a_buy_pays_out_and_goes_long` |
| SELL cash flow | `pnl_e2e_a_sell_takes_in_cash_and_goes_short` |
| Gün sonu mark to market | `pnl_e2e_the_day_is_marked_at_the_closing_price` |

Bunların her biri ayrıca onu uygulayan bileşene karşı da kontrol ediliyor:
`test_mold_udp64.cpp`, `test_itch_parser.cpp`, `test_gap_strategy.cpp` ve
`test_position.cpp`.

## Örnek veri

`itch_data_250815_HI2.dat`, 584 MB, 15 Ağustos 2025. Tek session, `TR29253481`.

| | |
|---|---|
| Packet | 519.492 |
| ITCH message | 15.560.041 |
| Dosyadaki order book sayısı | 1.769 |
| Sequence gap / duplicate | 0 / 0 |
| Target book 73616 | `EKGYO.E` |
| Target message (A / E / D) | 147.637 / 54.409 / 106.146 |
| Target book'ta tepe canlı order | 4.528 |
| Fiyat aralığı | 17.060 – 20.840 kuruş (3.780 kuruşluk band) |
| Target book'taki timestamp group'u | 197.449 |

Dosyada geçen message type'ları: `A D E Z P T O L Y R V S`. Dördü işleniyor, geri kalanı
atlanıyor.

Gün `P_SUREKLI_ISLEM` ile başlayıp `P_MARJ_YAYIN_KAPANIS` ile bitiyor, tam sekiz saat
arayla, ve her durum bir kez geçiyor. Timestamp group'larının çoğu küçük — %61'i tek
message, %37'si iki message taşıyor — ama en büyüğü 3.919 taşıyor, ki stratejinin bir
group'un ortasında asla çalışmaması gerektiğinin sebebi tam olarak bu.

Feed'de ayrıca target book için sıfır (undisclosed) quantity'li order ve market order
sentinel'i hiç geçmiyor. Yine de ikisi de ele alınıyor, çünkü protokol ikisini de
dışlamıyor.

`config.example` içindeki kapasiteler bu sayılardan, on kattan fazla boşluk bırakılarak
belirlendi.

## Bilinen kısıtlar

**`'C'` ve `'Y'` işlenmiyor.** Bu feed'de zararsız olduğu doğrulandı — Varsayımlar
bölümünde anlattım — ama `'C'` kullanan ya da continuous trading sırasında bir book'u
flush eden bir feed ikisini de gerektirirdi. `'C'` tıpkı `'E'` gibi quantity azaltıyor,
`'Y'` ise book'u temizliyor, yani ikisini de eklemek zor değil.

**Fill model iyimser, ve P&L o gözle okunmalı.** Üretilen her order kendi fiyatından
tamamen fill olmuş sayılıyor. Gerçek piyasada bir gap'e order koymak sana işlem değil,
kuyruk sırası kazandırıyor: bazı order'lar hiç fill olmuyor, ve fill olanlar ağırlıklı
olarak fiyatı senden uzağa iten tarafın doldurduğu order'lar oluyor. Buradan çıkan sayı
bir tahminden çok bir üst sınıra yakın.

**Sadece bir günlük veri kontrol edildi.** Yukarıdaki kapasite sayıları tek bir
session'dan geliyor. Daha yoğun bir gün daha büyük limitler isterdi — sabit değil de
config olmalarının sebebi bu.

**Bir gap, stratejiyi session'ın geri kalanı için durduruyor.** Snapshot feed'i olsaydı
book yeniden kurulup işleme devam edilebilirdi, ama tek başına ITCH bunu yapamıyor.

**Latency sayıları bir VM'in içinden geliyor.** Her şey WSL2 altında ölçüldü. Medyan ve
işe yarar percentile'lar kararlı, ama uzak kuyruk hypervisor'a ait; bare metal bir koşu
p99.9'un ötesinde farklı görünürdü.
