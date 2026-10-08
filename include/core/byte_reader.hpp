#ifndef LLOB_CORE_BYTE_READER_HPP
#define LLOB_CORE_BYTE_READER_HPP

// Bounds-checked, alignment-safe, big-endian field okuma.
//
// Her okuma byte byte yapılıyor: bu hem unaligned access sorununu hem de
// endianness'i tek seferde çözüyor, ve -O3 loop'u tek bir bswap/movbe
// instruction'ına geri katlıyor. Buffer'ı packed bir struct'a cast etmek yasak.
//
// Arayüzün özeti:
//   class ByteReader {
//       ByteReader(const uint8_t* data, size_t length);
//       bool     has(size_t n) const;        // kalan >= n
//       uint8_t  u8(size_t offset)  const;
//       uint16_t be16(size_t offset) const;
//       uint32_t be32(size_t offset) const;
//       uint64_t be64(size_t offset) const;
//       const uint8_t* raw(size_t offset) const;   // Alpha field'lar için
//   };
//
// Exception yok. Caller okumadan önce has() ile kontrol ediyor; başarısız bir
// kontrol throw değil, return value.
#include <cassert>
#include <cstdint>
#include <cstddef>


namespace llob {

class ByteReader {
public:
    ByteReader(const uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    bool        has(std::size_t n) const { return size_ >= n; }

    std::size_t size()             const { return size_; }

    uint8_t u8(std::size_t off) const {
      assert(off <size_);
      return data_[off];
    }
    uint16_t be16(std::size_t off) const {
    assert(off + 2 <= size_);
    uint16_t v = 0;
    for (std::size_t i = 0; i < 2; ++i)
      v = (v<<8) | data_[off+i];
    return v;
    }
    uint32_t be32(std::size_t off) const {
    assert(off + 4 <= size_);
    uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i)
      v = (v<<8) | data_[off+i];
    return v;
    }

    uint64_t be64(std::size_t off) const {
    assert(off + 8 <= size_);
    uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i)
        v = (v << 8) | data_[off + i];
    return v;
    }
    const uint8_t* raw(std::size_t off) const{
      assert(off <= size_);
      return data_ + off;
    }
    ByteReader slice(std::size_t off, std::size_t len) const{
      assert (off + len <= size_);
      return ByteReader(data_ +off, len);
    }
private:
    const uint8_t* data_;
    std::size_t    size_;
};

}



#endif // LLOB_CORE_BYTE_READER_HPP
