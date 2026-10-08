#include "test_framework.hpp"
#include "core/config.hpp"

#include <string>

using namespace llob;

namespace {

// A complete, valid file. Individual tests change one line at a time.
const char* GOOD =
    "# Statech - example configuration\n"
    "target_order_book_id = 73616\n"
    "tick_size            = 1\n"
    "\n"
    "order_quantity       = 100\n"
    "minimum_position     = -1000\n"
    "maximum_position     = 1000\n"
    "\n"
    "max_active_orders    = 1048576\n"
    "max_price_levels     = 65536\n";

} // namespace

TEST(config_reads_every_required_key) {
  Config c; std::string err;
  REQUIRE(parse_config(GOOD, c, err));

  CHECK_EQ(c.target_order_book_id, 73616u);
  CHECK_EQ(c.tick_size,            1);
  CHECK_EQ(c.order_quantity,       100ull);
  CHECK_EQ(c.minimum_position,     -1000);
  CHECK_EQ(c.maximum_position,     1000);
  CHECK_EQ(c.max_active_orders,    1048576u);
  CHECK_EQ(c.max_price_levels,     65536u);
}

TEST(config_keeps_a_negative_minimum_position) {
  // A short limit is a negative number. Reading it as unsigned would wrap and
  // the SELL quantity formula would then allow unlimited selling.
  Config c; std::string err;
  REQUIRE(parse_config(GOOD, c, err));
  CHECK(c.minimum_position < 0);
}

TEST(config_ignores_comments_blank_lines_and_spacing) {
  Config c; std::string err;
  const char* text =
      "\n"
      "   # a comment with leading spaces\n"
      "target_order_book_id=73616\n"            // no spaces at all
      "   tick_size   =   1   \n"               // spaces everywhere
      "order_quantity = 100\n"
      "minimum_position = -5\n"
      "maximum_position = 5\n"
      "max_active_orders = 10\n"
      "max_price_levels = 10\n";

  REQUIRE(parse_config(text, c, err));
  CHECK_EQ(c.target_order_book_id, 73616u);
  CHECK_EQ(c.tick_size, 1);
}

TEST(config_rejects_a_missing_key_instead_of_defaulting) {
  Config c; std::string err;
  const char* text =
      "target_order_book_id = 73616\n"
      "tick_size = 1\n"
      "order_quantity = 100\n"
      "minimum_position = -10\n"
      "maximum_position = 10\n"
      "max_active_orders = 10\n";       // max_price_levels is absent

  CHECK(!parse_config(text, c, err));
  CHECK(err.find("max_price_levels") != std::string::npos);
}

TEST(config_rejects_an_unknown_key) {
  // Catches typos. Silently ignoring the line would leave the real key unset.
  Config c; std::string err;
  std::string text = std::string(GOOD) + "max_price_level = 100\n";

  CHECK(!parse_config(text, c, err));
  CHECK(err.find("unknown key") != std::string::npos);
}

TEST(config_rejects_a_value_that_is_not_a_number) {
  Config c; std::string err;
  CHECK(!parse_config("target_order_book_id = abc\n", c, err));

  Config c2; std::string err2;
  CHECK(!parse_config("target_order_book_id = 100abc\n", c2, err2));
}

TEST(config_rejects_a_line_without_an_equals_sign) {
  Config c; std::string err;
  CHECK(!parse_config("target_order_book_id 73616\n", c, err));
  CHECK(err.find("key = value") != std::string::npos);
}

TEST(config_reads_the_file_shipped_with_the_assignment) {
  // Exactly as supplied, including the -1 capacities and the blank lines.
  Config c; std::string err;
  const char* text =
      "order_quantity       = 100\n"
      "\n"
      "minimum_position     = 0\n"
      "\n"
      "maximum_position     = 1000\n"
      "\n"
      "tick_size            = 10\n"
      "\n"
      "target_order_book_id = 73616\n"
      "\n"
      "max_active_orders    = -1\n"
      "\n"
      "max_price_levels     = -1\n";

  REQUIRE(parse_config(text, c, err));
  CHECK_EQ(c.order_quantity, 100ull);
  CHECK_EQ(c.minimum_position, 0);        // no short selling
  CHECK_EQ(c.maximum_position, 1000);
  CHECK_EQ(c.tick_size, 10);              // not the 1 the PDF shows
  CHECK_EQ(c.target_order_book_id, 73616u);
}

TEST(config_minus_one_turns_a_capacity_limit_off) {
  // The limit goes away, but a fixed-capacity design still has to allocate
  // something, so a documented default takes over.
  Config c; std::string err;
  const char* text =
      "target_order_book_id = 73616\n"
      "tick_size = 10\n"
      "order_quantity = 100\n"
      "minimum_position = 0\n"
      "maximum_position = 1000\n"
      "max_active_orders = -1\n"
      "max_price_levels = -1\n";

  REQUIRE(parse_config(text, c, err));
  CHECK(!c.orders_limited);
  CHECK(!c.levels_limited);
  CHECK_EQ(c.max_active_orders, DEFAULT_MAX_ACTIVE_ORDERS);
  CHECK_EQ(c.max_price_levels, DEFAULT_MAX_PRICE_LEVELS);
}

TEST(config_an_explicit_capacity_still_counts_as_a_limit) {
  Config c; std::string err;
  const char* text =
      "target_order_book_id = 73616\n"
      "tick_size = 10\n"
      "order_quantity = 100\n"
      "minimum_position = 0\n"
      "maximum_position = 1000\n"
      "max_active_orders = 500\n"
      "max_price_levels = 256\n";

  REQUIRE(parse_config(text, c, err));
  CHECK(c.orders_limited);
  CHECK(c.levels_limited);
  CHECK_EQ(c.max_active_orders, 500u);
  CHECK_EQ(c.max_price_levels, 256u);
}

TEST(config_still_rejects_other_non_positive_capacities) {
  // -1 is the one special value. -2 or 0 are simply unusable.
  Config c; std::string err;
  CHECK(!parse_config("max_active_orders = 0\n", c, err));
  CHECK(!parse_config("max_active_orders = -2\n", c, err));
  CHECK(!parse_config("max_price_levels = -5\n", c, err));
}

TEST(config_rejects_values_that_cannot_work) {
  Config c; std::string err;

  CHECK(!parse_config("tick_size = 0\n", c, err));            // would divide by zero
  CHECK(!parse_config("tick_size = -1\n", c, err));
  CHECK(!parse_config("order_quantity = 0\n", c, err));       // no order could be sent
  CHECK(!parse_config("max_active_orders = 0\n", c, err));    // pool with no room
  CHECK(!parse_config("max_price_levels = 0\n", c, err));
}

TEST(config_rejects_a_minimum_position_above_the_maximum) {
  Config c; std::string err;
  const char* text =
      "target_order_book_id = 73616\n"
      "tick_size = 1\n"
      "order_quantity = 100\n"
      "minimum_position = 500\n"
      "maximum_position = 100\n"        // the wrong way round
      "max_active_orders = 10\n"
      "max_price_levels = 10\n";

  CHECK(!parse_config(text, c, err));
  CHECK(err.find("minimum_position") != std::string::npos);
}

TEST(config_rejects_a_number_too_large_for_its_field) {
  Config c; std::string err;
  CHECK(!parse_config("target_order_book_id = 99999999999\n", c, err));

  Config c2; std::string err2;
  CHECK(!parse_config("tick_size = 99999999999\n", c2, err2));
}

TEST(config_reports_the_line_number_of_the_problem) {
  Config c; std::string err;
  const char* text =
      "target_order_book_id = 73616\n"
      "tick_size = 1\n"
      "order_quantity = oops\n";        // line 3

  CHECK(!parse_config(text, c, err));
  CHECK(err.find("line 3") != std::string::npos);
}

TEST(load_config_fails_cleanly_when_the_file_is_missing) {
  Config c; std::string err;
  CHECK(!load_config("no/such/config.txt", c, err));
  CHECK(err.find("cannot open") != std::string::npos);
}
