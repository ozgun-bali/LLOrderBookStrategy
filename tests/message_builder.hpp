#ifndef LLOB_TESTS_MESSAGE_BUILDER_HPP
#define LLOB_TESTS_MESSAGE_BUILDER_HPP

#include <cstdint>    
#include <cstddef>    
#include <vector>     
#include <cstring>    
#include <cassert>    

#include "core/types.hpp"  


// Builds synthetic MoldUDP64 packets and ITCH messages for tests.
//
// Worth writing first: it is the only way to exercise sequence gaps, truncated
// headers, invalid lengths, heartbeats, end of session and non-target books
// deterministically. It also unblocks everything if the sample data arrives late.
//
// Should be able to emit: a 'T' seconds message, Add / Executed / Delete / state
// messages with chosen timestamps, and to wrap any set of messages into a packet
// with a chosen session, sequence number and message count - including
// deliberately malformed ones.
namespace llobtest {

using namespace llob;
//DUMMY VALUES FOR NON USED FIELDS in ADD_ORDER
#define RANKSEQ 0xD1D1D1D1 
#define RANKINGTIME 0xD1D1D1D1D1D1D1D1

typedef std::vector<uint8_t> Bytes;

struct PacketHeader{ // MoldUDP64 Header struct
  const char* session;
  uint64_t seqNo;
  uint16_t mesCount;
};

struct MoldUDP64Packet {
  PacketHeader header;
  std::vector<Bytes> messages;
};

inline void put_u8 (Bytes& out, uint8_t v){
    out.push_back( v        & 0xFF);  

}
inline void put_be16 (Bytes& out, uint16_t v){
    out.push_back((v >>  8) & 0xFF);
    out.push_back( v        & 0xFF);  

}
inline void put_be32 (Bytes& out, uint32_t v){
    out.push_back((v >> 24) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >>  8) & 0xFF);
    out.push_back( v        & 0xFF);  

}
inline void put_be64 (Bytes& out, uint64_t v){
    out.push_back((v >> 56) & 0xFF);
    out.push_back((v >> 48) & 0xFF);
    out.push_back((v >> 40) & 0xFF);
    out.push_back((v >> 32) & 0xFF);
    out.push_back((v >> 24) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >>  8) & 0xFF);
    out.push_back( v        & 0xFF);    

}
inline void put_alpha(Bytes& out, const char* s, std::size_t width){
  std::size_t len = std::strlen(s);
  assert(width >= len);
  for (std::size_t i = 0; i < len; i++)
    out.push_back((uint8_t)s[i]);
  for (std::size_t i = len; i < width; i++)
    out.push_back(0x20);
  
 
}
inline char side_char(Side s) {
    assert(s == SIDE_BUY || s == SIDE_SELL);
    return s == SIDE_BUY ? 'B' : 'S';
}

inline Bytes deleted(uint32_t nanos, OrderId id, OrderBookId book, Side side){

Bytes deletebyte;
put_u8(deletebyte,'D');
put_be32(deletebyte,nanos);
put_be64(deletebyte,id);
put_be32(deletebyte,book);
put_u8(deletebyte,side_char(side));
return deletebyte;

}
inline Bytes seconds(uint32_t sec){
  Bytes secondBytes;
  put_u8(secondBytes,'T');
  put_be32(secondBytes,sec);
  return secondBytes;
}

inline Bytes book_state(uint32_t nanos, OrderBookId book, const char* state){
  Bytes bookBytes;
  put_u8(bookBytes, 'O');
  put_be32(bookBytes,nanos);
  put_be32(bookBytes,book);
  put_alpha(bookBytes,state,20);
  return bookBytes;
}
inline Bytes executed(uint32_t nanos, OrderId id,OrderBookId book,  Side side, Quantity qty, uint64_t match_id, uint32_t combo_id){

  Bytes executedBytes;
  put_u8(executedBytes, 'E');
  put_be32(executedBytes,nanos);
  put_be64(executedBytes,id);
  put_be32(executedBytes,book);
  put_u8(executedBytes,side_char(side));
  put_be64(executedBytes,qty);
  put_be64(executedBytes,match_id);
  put_be32(executedBytes,combo_id);
  for (size_t i = 0; i < 14; i++)
  {
    executedBytes.push_back(0x00);
  }
  return executedBytes;

}
inline Bytes add_order (uint32_t nanos, OrderId id, OrderBookId book, Side side,Quantity qty, Price price, uint16_t attrs = 0, uint8_t lot_type = 2){

  Bytes addedBytes;
  put_u8(addedBytes, 'A');
  put_be32(addedBytes,nanos);
  put_be64(addedBytes,id);
  put_be32(addedBytes,book);
  put_u8(addedBytes,side_char(side));
  put_be32(addedBytes,RANKSEQ);
  put_be64(addedBytes,qty);
  put_be32(addedBytes,(uint32_t)price);
  put_be16(addedBytes,attrs);
  put_u8(addedBytes,lot_type);
  put_be64(addedBytes,RANKINGTIME);
  return addedBytes;


}

// ---------------------------------------------------------------- MoldUDP64

inline void put_bytes(Bytes& out, const Bytes& in) {
    out.insert(out.end(), in.begin(), in.end());
}

// Fills mesCount from the message list. Tests that need a wrong count, a
// heartbeat or an end-of-session marker overwrite header.mesCount afterwards.
inline MoldUDP64Packet make_packet(const char* session, uint64_t seq,
                                   const std::vector<Bytes>& msgs) {
    MoldUDP64Packet p;
    p.header.session  = session;
    p.header.seqNo    = seq;
    p.header.mesCount = (uint16_t)msgs.size();
    p.messages        = msgs;
    return p;
}

// 20-byte header, then one [2-byte length][payload] block per message.
// mesCount is written as given, so a deliberately wrong count still serializes.
inline Bytes serialize(const MoldUDP64Packet& p) {
    Bytes out;
    put_alpha(out, p.header.session, 10);
    put_be64 (out, p.header.seqNo);
    put_be16 (out, p.header.mesCount);

    for (std::size_t i = 0; i < p.messages.size(); ++i) {
        const Bytes& m = p.messages[i];
        assert(m.size() <= 0xFFFF);
        put_be16(out, (uint16_t)m.size());
        put_bytes(out, m);
    }
    return out;
}

// ------------------------------------------------- deliberate corruption
// Build a valid packet, then break exactly one thing. Keeps the normal path
// clean and makes each test say plainly what it is exercising.

inline void truncate_to(Bytes& wire, std::size_t n) {
    assert(n <= wire.size());
    wire.resize(n);
}

inline void set_message_count(Bytes& wire, uint16_t n) {
    assert(wire.size() >= 20);
    wire[18] = (uint8_t)((n >> 8) & 0xFF);
    wire[19] = (uint8_t)( n       & 0xFF);
}

// Rewrites the declared length of block `index` without touching its payload,
// so the stated and actual sizes disagree.
inline void set_block_length(Bytes& wire, std::size_t index, uint16_t len) {
    std::size_t off = 20;
    for (std::size_t i = 0; i < index; ++i) {
        assert(off + 2 <= wire.size());
        std::size_t blockLen = ((std::size_t)wire[off] << 8) | wire[off + 1];
        off += 2 + blockLen;
    }
    assert(off + 2 <= wire.size());
    wire[off]     = (uint8_t)((len >> 8) & 0xFF);
    wire[off + 1] = (uint8_t)( len       & 0xFF);
}


// TODO

} // namespace llobtest

#endif // LLOB_TESTS_MESSAGE_BUILDER_HPP
