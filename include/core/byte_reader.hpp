#ifndef LLOB_CORE_BYTE_READER_HPP
#define LLOB_CORE_BYTE_READER_HPP

// Bounds-checked, alignment-safe, big-endian field extraction.
//
// Every read is byte-by-byte: this solves unaligned access and endianness at
// once, and -O3 folds it back into a single bswap/movbe. Never cast the buffer
// to a packed struct.
//
// Interface sketch:
//   class ByteReader {
//       ByteReader(const uint8_t* data, size_t length);
//       bool     has(size_t n) const;        // remaining >= n
//       uint8_t  u8(size_t offset)  const;
//       uint16_t be16(size_t offset) const;
//       uint32_t be32(size_t offset) const;
//       uint64_t be64(size_t offset) const;
//       const uint8_t* raw(size_t offset) const;   // for Alpha fields
//   };
//
// No exceptions. Callers check has() before reading; a failed check is a
// status return, not a throw.

#include <cstddef>
#include <cstdint>

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_CORE_BYTE_READER_HPP
