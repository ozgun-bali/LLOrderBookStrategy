#ifndef LLOB_IO_DATAGRAM_SOURCE_HPP
#define LLOB_IO_DATAGRAM_SOURCE_HPP

// Finds where one MoldUDP64 packet ends and the next begins.
//
// Over the wire this question never comes up: MoldUDP64 runs on UDP, and the
// operating system hands over one complete datagram at a time with its length.
// That is why process_datagram takes a pointer and a length rather than
// working out the boundary itself.
//
// Reading from a file there is no such envelope - 584 MB of packets glued
// together with nothing between them. The boundary has to be recovered, and it
// can only be computed:
//
//     packet length = 20 + sum over blocks of (2 + block length)
//
// Confirmed against the sample feed: no pcap magic number, and walking it this
// way lands exactly on the last byte after 519,492 packets.
//
// Only the lengths are read here, never the payloads: the message count, then
// two bytes per block. About 62 bytes to establish that a 248-byte packet is
// 248 bytes long, and those same bytes are in L1 by the time MoldSession reads
// them again.
//
// Keeping this separate means a feed stored as pcap, or with length-prefixed
// records, is a change to one file.

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

    // True when the walk could not reach the end: a header or a block running
    // past the buffer. Everything after that point is unreadable, because the
    // next boundary can no longer be computed.
    bool truncated() const { return truncated_; }

    // Hands out a view into the buffer; nothing is copied. Returns false at the
    // end of the data, or once the walk has broken.
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
    // Walks the block lengths to find the total. Reads no payload.
    bool measure(std::size_t start, std::size_t& outLen) const {
        if (start + MOLD_HEADER_LEN > size_) return false;

        const uint16_t count = be16(start + MOLD_OFF_COUNT);

        // A heartbeat carries no blocks, and the end-of-session marker is not
        // a count at all. Both are a bare header.
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
