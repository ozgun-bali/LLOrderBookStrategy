#ifndef LLOB_CORE_CONFIG_HPP
#define LLOB_CORE_CONFIG_HPP

// Runtime config. Açılışta bir kez yükleniyor - cold path, yani std::string ve
// dosya I/O burada sorun değil. Hot path'in güvendiği her capacity bu struct'tan
// sabitleniyor ve sonrasında asla büyümüyor.
//
// Her key zorunlu. Eksik ya da yanlış yazılmış bir key hata sayılıyor, sessizce
// default'a düşülmüyor: yanlış bir capacity aksi halde çok daha sonra,
// anlaşılması zor bir runtime hatası olarak ortaya çıkardı.

#include "core/types.hpp"

#include <string>

namespace llob {

// Capacity olarak -1 verilmesi "config bir limit koymuyor" demek. Fixed-capacity
// bir tasarımın yine de bir şeyler allocate etmesi gerektiği için seçilen değerler
// bunlar: sample data'dan ölçüldü, hedef order book en fazla 4.528 aktif order ve
// 3.780 tick genişliğinde bir bant gördü, üzerine on kattan fazla headroom
// bırakıldı.
static const std::size_t DEFAULT_MAX_ACTIVE_ORDERS = 1u << 16;   // 65.536
static const std::size_t DEFAULT_MAX_PRICE_LEVELS  = 1u << 14;   // 16.384

struct Config {
    OrderBookId target_order_book_id;
    Price       tick_size;

    Quantity    order_quantity;
    Position    minimum_position;
    Position    maximum_position;

    std::size_t max_active_orders;
    std::size_t max_price_levels;

    // Config -1 dediğinde false. Capacity yine de sonlu, ama aşılması "configure
    // edilen limite ulaşıldı" değil, config'den bağımsız bir yetersizlik anlamına
    // geliyor.
    bool orders_limited;
    bool levels_limited;

    Config()
        : target_order_book_id(0), tick_size(0), order_quantity(0),
          minimum_position(0), maximum_position(0),
          max_active_orders(DEFAULT_MAX_ACTIVE_ORDERS),
          max_price_levels(DEFAULT_MAX_PRICE_LEVELS),
          orders_limited(true), levels_limited(true) {}
};

// false döner ve `error`'a hangi satır, ne sorun olduğunu yazar.
// Exception yok: projenin geri kalanı hataları return value olarak ele alıyor.
bool load_config(const char* path, Config& out, std::string& error);

// Dosyasız aynı kontroller, testler text'i doğrudan verebilsin diye.
bool parse_config(const std::string& text, Config& out, std::string& error);

} // namespace llob

#endif // LLOB_CORE_CONFIG_HPP
