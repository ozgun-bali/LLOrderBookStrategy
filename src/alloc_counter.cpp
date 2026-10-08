// Global allocation operator'larını değiştiriyor ki her heap isteği sayılsın.
//
// Bunlar standardın "replaceable" ilan ettiği fonksiyonlar: burada tanımlamak
// linker'ın kütüphaneninki yerine bizimkini kullanmasını sağlıyor - bütün program
// ve içeri çektiği her kütüphane için. Bir allocation'ın bunların etrafından
// dolaşmasının yolu yok.
//
// Saymanın kendisi düz bir size_t'nin bir increment'i. Override'ın bunun ötesinde
// bir maliyeti yok: libstdc++'ın kendi operator new'i de bir malloc çağrısı artı
// başarısızlıkta bir throw, yani aşağıdakinin aynısı.

#include "app/alloc_counter.hpp"

#include <cstdlib>
#include <new>

namespace llob {
std::size_t g_alloc_count = 0;
std::size_t g_alloc_bytes = 0;
}

void* operator new(std::size_t n) {
    ++llob::g_alloc_count;
    llob::g_alloc_bytes += n;

    // malloc(0)'ın null dönmesine izin var, ve burada bu başarısızlık gibi görünürdü.
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t n) { return operator new(n); }

void* operator new(std::size_t n, const std::nothrow_t&) throw() {
    ++llob::g_alloc_count;
    llob::g_alloc_bytes += n;
    return std::malloc(n ? n : 1);
}

void* operator new[](std::size_t n, const std::nothrow_t& t) throw() {
    return operator new(n, t);
}

// Free sayılmıyor: soru, programın hot path'te ne sıklıkla bellek İSTEDİĞİ, ve
// bırakmak bir istek değil.
void operator delete(void* p) throw() { std::free(p); }
void operator delete[](void* p) throw() { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) throw() { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) throw() { std::free(p); }
