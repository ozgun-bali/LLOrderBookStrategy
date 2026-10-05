#ifndef LLOB_IO_DATAGRAM_SOURCE_HPP
#define LLOB_IO_DATAGRAM_SOURCE_HPP

// Splits the input file into individual MoldUDP64 datagrams.
//
// OPEN QUESTION: how packets are framed inside the sample file is not specified
// anywhere. Three plausible options:
//   1. raw concatenation - self-delimiting, since a MoldUDP64 packet length is
//      20 + sum(2 + block_length) over message_count blocks
//   2. pcap / pcapng
//   3. explicit length-prefixed records
// Keeping this behind an interface means swapping framing when the real sample
// data arrives costs one file, not a rewrite.
//
// The whole file is read into memory once, before any timed region. This type
// hands out (pointer, length) views into that buffer - it never copies.

#include <cstddef>
#include <cstdint>

namespace llob {

// TODO

} // namespace llob

#endif // LLOB_IO_DATAGRAM_SOURCE_HPP
