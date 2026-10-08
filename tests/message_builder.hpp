#ifndef LLOB_TESTS_MESSAGE_BUILDER_HPP
#define LLOB_TESTS_MESSAGE_BUILDER_HPP

#include <cstdint>    
#include <cstddef>    
#include <vector>     
#include <cstring>    
#include <cassert>    

#include "core/types.hpp"  


// Testler için sentetik MoldUDP64 packet'leri ve ITCH message'ları üretiyor.
//
// İlk yazılmaya değer parça: sequence gap'leri, kesik header'ları, geçersiz
// length'leri, heartbeat'leri, end of session'ı ve hedef dışı book'ları
// deterministik olarak denemenin tek yolu bu. Ayrıca sample data geç gelirse
// her şeyin önünü açıyor.
//
// Üretebilmesi gerekenler: bir 'T' seconds message'ı, seçilen timestamp'lerle
// Add / Executed / Delete / state message'ları, ve herhangi bir message
// kümesini seçilen session, sequence number ve message count ile bir packet'e
// sarmak - bilerek bozulmuş olanlar dahil.
namespace llobtest {

using namespace llob;
//ADD_ORDER'DA KULLANILMAYAN FIELD'LAR ICIN DUMMY DEGERLER
#define RANKSEQ 0xD1D1D1D1 
#define RANKINGTIME 0xD1D1D1D1D1D1D1D1

typedef std::vector<uint8_t> Bytes;

struct PacketHeader{ // MoldUDP64 header struct'ı
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


inline void put_bytes(Bytes& out, const Bytes& in) {
    out.insert(out.end(), in.begin(), in.end());
}

inline MoldUDP64Packet make_packet(const char* session, uint64_t seq,
                                   const std::vector<Bytes>& msgs) {
    MoldUDP64Packet p;
    p.header.session  = session;
    p.header.seqNo    = seq;
    p.header.mesCount = (uint16_t)msgs.size();
    p.messages        = msgs;
    return p;
}


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


inline void truncate_to(Bytes& wire, std::size_t n) {
    assert(n <= wire.size());
    wire.resize(n);
}

inline void set_message_count(Bytes& wire, uint16_t n) {
    assert(wire.size() >= 20);
    wire[18] = (uint8_t)((n >> 8) & 0xFF);
    wire[19] = (uint8_t)( n       & 0xFF);
}

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

// Packet'leri tam olarak feed dosyasının yaptığı gibi yapıştırıyor: aralarında
// hiçbir şey yok, index yok, length prefix'i yok. Sınırın block length'lerinden
// yeniden hesaplanması gerekiyor - DatagramSource tam olarak bunun için var.
inline Bytes glue(const std::vector<Bytes>& packets) {
    Bytes all;
    for (std::size_t i = 0; i < packets.size(); ++i)
        all.insert(all.end(), packets[i].begin(), packets[i].end());
    return all;
}

// Bütün bir feed kuruyor, sequence number'ı senin yerine tutarak.
//
// MoldUDP64 packet değil message sayıyor, yani bir sonraki packet'in sequence'i
// bu packet'inki artı içindeki message sayısı. Bunu elle yanlış hesaplamak,
// yanlış sebepten geçen bir test yazmanın en kolay yolu.
struct Feed {
    Bytes    wire;
    uint64_t seq;
    const char* session;

    explicit Feed(const char* s = "TESTSESS01") : seq(1), session(s) {}

    // Bu message'ları sırayla taşıyan tek bir packet.
    void packet(const std::vector<Bytes>& messages) {
        append(serialize(make_packet(session, seq, messages)));
        seq += messages.size();
    }

    // Hiç block'u olmayan bir packet. Sequence'i beklenen bir sonraki, yani
    // hiçbir şey taşımadan stream'in canlı olduğunu duyuruyor.
    void heartbeat() { append(serialize(make_packet(session, seq, {}))); }

    // End of session marker'ı: count'u 0xFFFF olan çıplak bir header.
    void end_of_session() {
        Bytes p = serialize(make_packet(session, seq, {}));
        set_message_count(p, 0xFFFF);
        append(p);
    }

    // n message hiç gelmemiş gibi yapıyor; bir sequence gap böyle üretiliyor.
    void lose(uint64_t n) { seq += n; }

    void append(const Bytes& b) { wire.insert(wire.end(), b.begin(), b.end()); }
};

} // namespace llobtest

#endif // LLOB_TESTS_MESSAGE_BUILDER_HPP
