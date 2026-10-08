#ifndef LLOB_IO_DATAGRAM_SOURCE_HPP
#define LLOB_IO_DATAGRAM_SOURCE_HPP

// Bir MoldUDP64 packet'inin nerede bitip bir sonrakinin nerede başladığını bulur.
//
// Wire üzerinde bu soru hiç ortaya çıkmıyor: MoldUDP64 UDP üzerinde koşuyor ve
// işletim sistemi her seferinde tam bir datagram'ı uzunluğuyla birlikte veriyor.
// process_datagram'ın sınırı kendi hesaplamak yerine bir pointer ve bir length
// almasının sebebi bu.
//
// Dosyadan okurken böyle bir zarf yok - aralarında hiçbir şey olmadan
// yapıştırılmış 584 MB'lık packet. Sınırın geri kazanılması gerekiyor, ve bu
// ancak hesaplanabilir:
//
//     packet length = 20 + her block için (2 + block length) toplamı
//
// Sample data üzerinde doğrulandı: pcap magic number yok, ve bu şekilde
// yürümek 519.492 packet sonra tam olarak son byte'a oturuyor.
//
// Burada sadece length'ler okunuyor, payload'lar asla: önce message count, sonra
// block başına iki byte. 248 byte'lık bir packet'in 248 byte olduğunu tespit
// etmek yaklaşık 62 byte okumak demek, ve MoldSession onları tekrar okuduğunda
// aynı byte'lar zaten L1'de oluyor.
//
// Bunu ayrı tutmak, pcap olarak saklanmış ya da length-prefix'li kayıtlardan
// oluşan bir feed'in tek bir dosyalık değişiklik olması anlamına geliyor.

#include "core/types.hpp"
#include "protocol/mold_udp64.hpp"

#include <cstddef>

namespace llob {

class DatagramSource {
public:
    DatagramSource(const uint8_t* data, std::size_t size)
        : data_(data), size_(size), offset_(0), packets_(0), truncated_(false) {}

    std::size_t offset()    const { return offset_; }
    uint64_t    packets()   const { return packets_; }

    // Yürüyüş sona ulaşamadığında true: buffer'ın dışına taşan bir header ya da
    // block. O noktadan sonrası okunamaz, çünkü bir sonraki sınır artık
    // hesaplanamıyor.
    bool truncated() const { return truncated_; }

    // Buffer'ın içine bir view veriyor; hiçbir şey kopyalanmıyor. Data bittiğinde
    // ya da yürüyüş bozulduğunda false dönüyor.
    bool next(const uint8_t*& out, std::size_t& len) {
        if (truncated_ || offset_ >= size_) return false;

        std::size_t packetLen = 0;
        if (!measure(offset_, packetLen)) { truncated_ = true; return false; }

        out = data_ + offset_;
        len = packetLen;
        offset_ += packetLen;
        ++packets_;
        return true;
    }

private:
    // Toplamı bulmak için block length'leri yürüyor. Hiç payload okumuyor.
    bool measure(std::size_t start, std::size_t& outLen) const {
        if (start + MOLD_HEADER_LEN > size_) return false;

        const uint16_t count = be16(start + MOLD_OFF_COUNT);

        // Heartbeat hiç block taşımıyor, end of session marker'ı ise zaten bir
        // count değil. İkisi de çıplak bir header.
        if (count == MOLD_COUNT_HEARTBEAT || count == MOLD_COUNT_END_OF_SESSION) {
            outLen = MOLD_HEADER_LEN;
            return true;
        }

        std::size_t p = start + MOLD_HEADER_LEN;
        for (uint16_t i = 0; i < count; ++i) {
            if (p + 2 > size_) return false;
            const std::size_t blockLen = be16(p);
            if (p + 2 + blockLen > size_) return false;
            p += 2 + blockLen;
        }
        outLen = p - start;
        return true;
    }

    uint16_t be16(std::size_t at) const {
        return (uint16_t)(((uint16_t)data_[at] << 8) | data_[at + 1]);
    }

    const uint8_t* data_;
    std::size_t    size_;
    std::size_t    offset_;
    uint64_t       packets_;
    bool           truncated_;
};

} // namespace llob

#endif // LLOB_IO_DATAGRAM_SOURCE_HPP
