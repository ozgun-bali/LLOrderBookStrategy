#include "test_framework.hpp"
#include "message_builder.hpp"
#include "protocol/itch_messages.hpp"

using namespace llob;
using namespace llobtest;

TEST(decode_add_order_reads_every_field_the_book_needs) {
  Bytes m = add_order(10000000u, 77ull, 73616u, SIDE_SELL, 500ull, 104900);
  ByteReader r(&m[0], m.size());
  REQUIRE(r.has(LEN_ADD_ORDER));

  AddOrderMsg a;
  decode_add_order(r, a);

  CHECK_EQ(a.nanos,    10000000u);
  CHECK_EQ(a.order_id, 77ull);
  CHECK_EQ(a.side,     SIDE_SELL);
  CHECK_EQ(a.quantity, 500ull);
  CHECK_EQ(a.price,    104900);
}

TEST(decode_add_order_keeps_the_market_order_sentinel_negative) {
  Bytes m = add_order(1u, 1ull, 73616u, SIDE_BUY, 1ull, PRICE_NOT_AVAILABLE);
  ByteReader r(&m[0], m.size());
  REQUIRE(r.has(LEN_ADD_ORDER));

  AddOrderMsg a;
  decode_add_order(r, a);
  CHECK_EQ(a.price, PRICE_NOT_AVAILABLE);
  CHECK(a.price < 0);
}

TEST(decode_order_executed_and_delete) {
  Bytes e = executed(2u, 99ull, 73616u, SIDE_BUY, 250ull, 7ull, 0u);
  ByteReader re(&e[0], e.size());
  REQUIRE(re.has(LEN_ORDER_EXECUTED));

  OrderExecutedMsg x;
  decode_order_executed(re, x);
  CHECK_EQ(x.nanos,    2u);
  CHECK_EQ(x.order_id, 99ull);
  CHECK_EQ(x.side,     SIDE_BUY);
  CHECK_EQ(x.quantity, 250ull);

  Bytes d = deleted(3u, 99ull, 73616u, SIDE_SELL);
  ByteReader rd(&d[0], d.size());
  REQUIRE(rd.has(LEN_ORDER_DELETE));

  OrderDeleteMsg y;
  decode_order_delete(rd, y);
  CHECK_EQ(y.nanos,    3u);
  CHECK_EQ(y.order_id, 99ull);
  CHECK_EQ(y.side,     SIDE_SELL);
}

TEST(decode_book_state_maps_both_states_the_strategy_cares_about) {
  Bytes a = book_state(1u, 73616u, "P_SUREKLI_ISLEM");
  ByteReader ra(&a[0], a.size());
  REQUIRE(ra.has(LEN_BOOK_STATE));
  BookStateMsg sa;
  decode_book_state(ra, sa);
  CHECK_EQ(sa.state, STATE_SUREKLI_ISLEM);

  Bytes b = book_state(2u, 73616u, "P_MARJ_YAYIN_KAPANIS");
  ByteReader rb(&b[0], b.size());
  REQUIRE(rb.has(LEN_BOOK_STATE));
  BookStateMsg sb;
  decode_book_state(rb, sb);
  CHECK_EQ(sb.state, STATE_MARJ_YAYIN_KAPANIS);
  CHECK_EQ(sb.nanos, 2u);
}

TEST(decode_book_state_treats_an_unknown_name_as_other) {
  // Any state that is neither continuous trading nor end of day must not be
  // mistaken for one of them: the strategy may only run in P_SUREKLI_ISLEM.
  Bytes m = book_state(1u, 73616u, "P_ACILIS_SEANSI");
  ByteReader r(&m[0], m.size());
  REQUIRE(r.has(LEN_BOOK_STATE));

  BookStateMsg s;
  decode_book_state(r, s);
  CHECK_EQ(s.state, STATE_OTHER);
}

TEST(a_state_name_that_only_shares_a_prefix_is_not_a_match) {
  // "P_SUREKLI" padded out must not match "P_SUREKLI_ISLEM" padded out:
  // the comparison covers all 20 bytes, padding included.
  Bytes m = book_state(1u, 73616u, "P_SUREKLI");
  ByteReader r(&m[0], m.size());
  REQUIRE(r.has(LEN_BOOK_STATE));

  BookStateMsg s;
  decode_book_state(r, s);
  CHECK_EQ(s.state, STATE_OTHER);
}

TEST(side_from_char_rejects_anything_that_is_not_b_or_s) {
  CHECK_EQ(side_from_char('B'), SIDE_BUY);
  CHECK_EQ(side_from_char('S'), SIDE_SELL);
  CHECK_EQ(side_from_char('b'), SIDE_INVALID);   // case matters
  CHECK_EQ(side_from_char('X'), SIDE_INVALID);
  CHECK_EQ(side_from_char(0),   SIDE_INVALID);
  CHECK_EQ(side_from_char(0xFF),SIDE_INVALID);
}

TEST(expected_length_covers_handled_types_and_rejects_the_rest) {
  CHECK_EQ(expected_length(MSG_SECONDS),        LEN_SECONDS);
  CHECK_EQ(expected_length(MSG_BOOK_STATE),     LEN_BOOK_STATE);
  CHECK_EQ(expected_length(MSG_ADD_ORDER),      LEN_ADD_ORDER);
  CHECK_EQ(expected_length(MSG_ORDER_EXECUTED), LEN_ORDER_EXECUTED);
  CHECK_EQ(expected_length(MSG_ORDER_DELETE),   LEN_ORDER_DELETE);

  CHECK_EQ(expected_length(MSG_EXECUTED_PRICE), 0);   // 'C', skipped
  CHECK_EQ(expected_length(MSG_BOOK_FLUSH),     0);   // 'Y', skipped
  CHECK_EQ(expected_length('R'),                0);
  CHECK_EQ(expected_length(0),                  0);
}

TEST(the_book_id_offset_differs_between_state_and_the_order_messages) {
  // The early filter depends on this: 'O' carries it at 5, 'A'/'E'/'D' at 13.
  Bytes s = book_state(1u, 73616u, "P_SUREKLI_ISLEM");
  ByteReader rs(&s[0], s.size());
  CHECK_EQ(rs.be32(OFF_STATE_BOOK_ID), 73616u);

  Bytes a = add_order(1u, 1ull, 73616u, SIDE_BUY, 1ull, 100);
  ByteReader ra(&a[0], a.size());
  CHECK_EQ(ra.be32(OFF_ADD_BOOK_ID), 73616u);

  Bytes e = executed(1u, 1ull, 73616u, SIDE_BUY, 1ull, 1ull, 0u);
  ByteReader re(&e[0], e.size());
  CHECK_EQ(re.be32(OFF_EXE_BOOK_ID), 73616u);

  Bytes d = deleted(1u, 1ull, 73616u, SIDE_BUY);
  ByteReader rd(&d[0], d.size());
  CHECK_EQ(rd.be32(OFF_DEL_BOOK_ID), 73616u);
}
