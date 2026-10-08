# Low Latency Order Book and Strategy

MoldUDP64 / BISTECH ITCH market data replay, order book reconstruction, one-tick
gap strategy and P&L simulation.

## Build

```
make          # release, -O3 -DNDEBUG
make debug    # -O0 -g, invariant checks enabled
make check    # build and run tests
```

Target toolchain: GCC 8.5.0, C++11, Linux x86-64. No external dependencies.

## Run

```
./orderbook --input sample.bin --config config.txt [--orders orders.csv]
taskset -c 4 ./orderbook --benchmark sample.bin --iterations 50 --config config.txt
```

The first prints the summary to stdout and writes the orders to a CSV. The
second replays the data and reports throughput, latency, allocations and the
checksum. Pin it to a core: without that the scheduler moves the thread
between cores and the tail picks up cold caches that have nothing to do with
the code.

## Results on the sample feed

`itch_data_250815_HI2.dat` with the supplied configuration, 584 MB in about
three seconds:

```
final_position          = 300
cash_flow               = -6157000
final_pnl               = -409000
last_execution_price    = 19150
end_of_day_mark_price   = 19160
generated_order_count   = 1491          (747 buy, 744 sell)
checksum                = 0x45F146A704E0878C
```

The P&L reconciles: `-6,157,000 + 300 x 19,160 = -409,000`. The mark uses the
price at the close, not the last price in the file, which is why both are
printed.

Where the 197,463 evaluations went:

| Outcome | Count |
|---|---|
| spread was not two ticks | 179,951 |
| gap was not opened by that group | 15,532 |
| position limit left no room | 380 |
| outside continuous trading | 109 |
| **order sent** | **1,491** |

Running it twice gives byte-identical output, summary and CSV.

**The strategy loses money on this day.** That is what the fill model implies
rather than a bug: every order is treated as filled, while in a real market the
orders that fill are disproportionately the ones you would rather had not. See
Known limitations.

## Architecture

Data moves in one direction. Each step does one job and passes the result on.

```
input file
   -> DatagramSource    split the file into MoldUDP64 packets
   -> MoldSession       check the header, follow sequence numbers, cut out blocks
   -> ItchParser        read the message type, keep the clock, drop other books
   -> OrderBook         apply add / execute / delete, track best bid and ask
   -> Engine            close a timestamp group, then run the strategy once
   -> GapStrategy       produce an order when the rules allow it
   -> FillSimulator     turn that order into a fill
   -> Position          cash flow, position, end of day P&L
```

Each step only knows about the next one through a template parameter, not through a
base class. The call is resolved at compile time, so there is no virtual dispatch in
the hot path, and tests can put a small recorder in place of the real next step.

| Component | What it is responsible for |
|---|---|
| `core/byte_reader.hpp` | safe reads from a byte buffer: bounds, big-endian, no alignment assumptions |
| `core/types.hpp` | the scalar types and enums everything else shares |
| `protocol/itch_messages.hpp` | field offsets, message lengths, decoding into small structs |
| `protocol/mold_udp64.hpp` | packet framing, session tracking, sequence state machine |
| `protocol/itch_parser.hpp` | type dispatch, the `'T'` clock, the target book filter |

The order book, strategy, fill model and P&L are the remaining pieces.

## Data structures and why

**`ByteReader` is a view, not a copy.** It holds a pointer and a length. `slice()`
makes a smaller view over the same memory, so cutting a message out of a packet costs
nothing. The input file is read into memory once and every reader points into it.

**Reads go one byte at a time.** A cast like `*(uint32_t*)(p + 13)` would be wrong three
times over: the file is big-endian while x86 is little-endian, offset 13 is not aligned
for a 4-byte read, and a cast does not check that the bytes exist. Reading byte by byte
and shifting solves all three. `-O3` turns the loop back into a single `bswap`, so the
clearer code costs nothing.

**Bounds are checked once per message, not once per field.** Every handled message has a
fixed length, so one `has(45)` proves that all eleven fields of an Add Order are inside
the buffer. That is one comparison instead of eleven. The field readers keep an `assert`
as a safety net, which disappears under `-DNDEBUG`.

**Handlers are template parameters, not virtual interfaces.** A virtual call would cost a
table lookup per message and would stop the compiler from inlining. With a template the
compiler builds one copy per handler type and can see straight through the call. The same
choice makes the protocol layer testable without an order book behind it.

**The hot path lives in headers.** Without link-time optimisation, GCC cannot inline
across `.cpp` files. Keeping parsing, the book and the strategy in headers lets it inline
the whole chain from one message to one strategy decision. Only cold code — config
loading, reporting, the benchmark harness, `main` — sits in `src/`.

**Orders live in a fixed pool, found through a hash index.** The pool is one array of
slots sized from `max_active_orders`. Free slots form a linked list whose "next" pointer
is stored inside the unused slot itself, so taking and returning a slot are both O(1) and
cost no extra memory. A separate index maps an order id to a slot number.

`std::unordered_map` was the obvious alternative and was rejected on three counts: it
allocates a node per order, which is per-message allocation; its nodes sit wherever the
allocator put them, so every lookup is a pointer chase into cold memory; and it rehashes
when it grows, which is an unpredictable pause in the middle of a feed.

The index is open addressing with linear probing over a power-of-two table kept at most
half full, so the average lookup touches about one or two buckets. Deletion shifts
entries back rather than leaving tombstones. Tombstones are simpler, but orders are
removed constantly here and the graveyard would slowly turn every probe into a scan.

**There is one index per side, but one shared pool.** An order takes a slot whatever side
it is on, so the pool is shared and the total is bounded by one number. The index has to
be per side because ITCH order ids are unique per book *and side*: the same id can be
live on both sides at once, and a single table would call the second one a duplicate.

**Price levels are a dense array indexed by `price - base_price`.** Prices are whole
kurus with `tick_size` 1, so the level is reached by one subtraction. No search, no
comparison, nothing reordered. A sorted vector would put the best price at index 0 for
free, but the dominant operation is order id to price to level, and that would be a
binary search every time; worse, insert and erase shift indices, so an order could not
hold on to where its level lives. The sample feed settles the memory question too: the
target book moved across 3,780 ticks in a day, so the array is small enough to stay in
cache.

`base_price` is fixed from the first price seen, centred in the band. A price outside the
band is a safe error rather than a write past the array.

**The two sides are separate arrays.** One array with a side tag per level would halve
the memory, but inside a timestamp group a price really can be on both sides at once:
adding the new ask before deleting the old bid is a legal order of events, and a single
tag cannot represent it.

**Best bid and ask move incrementally.** A new level can only improve the best, so adding
one costs a single comparison. When the best level empties, the next one is found through
a bitset with one bit per level: one 64-bit word answers for 64 levels at a time, and
`__builtin_ctzll` picks the bit out in one instruction. Walking levels one at a time
would be fine most of the time and occasionally terrible, which is the shape that ruins a
tail latency.

## Performance

Fifty passes over the 584 MB feed, pinned to one core. Full method and
environment in [benchmark_report.md](benchmark_report.md).

| | |
|---|---|
| Best pass | 0.101 s for 15,560,041 messages |
| Throughput | 154 M messages/second, 6.5 ns per message |
| Per-datagram latency | p50 119 ns, p99 459 ns, p99.9 1,039 ns |
| **Hot-path allocations** | **0** |
| Peak memory | 577 MB, of which 557 MiB is the input file |
| Checksum | identical on all fifty passes |

Zero allocations is a measurement rather than a claim: the global
`operator new` is replaced, so every allocation anywhere in the program is
counted. The counter is reset once the pipeline is built and read when the
pass ends.

Most of the throughput is messages being thrown away. 98% of the feed belongs
to other order books and is rejected after reading the type byte, checking
the length and comparing one 32-bit field, so the 6.5 ns average is mostly
the cost of saying no. A target message costs considerably more.

The gap between p50 and p99.9 is where the design shows. The occupancy bitset
stops the best-price search turning into a scan when a level empties with a
gap above it, and backward-shift deletion keeps hash probes short however many
orders have been and gone. Both were chosen for the tail, not the average.

The reported maximum, 8.3 ms, is the hypervisor rather than the code: this
runs under WSL2 and the host can deschedule the whole guest. Packets are
uniform at 30 messages each, so there is no large datagram to blame.

## Complexity

| Step | Time | Space |
|---|---|---|
| Read one field | O(1) | none |
| Frame one packet | O(blocks) | none |
| Dispatch one message | O(1) | one small struct on the stack |
| Filter a non-target message | O(1), two reads | none |

Nothing in the protocol layer scans or sorts. A packet costs one pass over its blocks,
and a message costs a fixed number of reads.

The order book and the layers above it:

| Step | Time | Space |
|---|---|---|
| Add an order | O(1) expected | one 24-byte slot |
| Find an order by id | O(1) expected | none |
| Remove an order | O(1) expected, plus the probe run it shifts back | none |
| Add or remove quantity at a level | O(1) | none |
| Best price, when the level stays occupied | O(1) | none |
| Best price, after the level empties | O(gap / 64) words of the bitset | none |
| Mark a level as touched in a group | O(1) | none |
| Start a new group | O(1) | none |
| Evaluate the strategy for one level | O(1) | none |
| Record a fill | O(1) | none |

"Expected" on the hash operations means the probe run is short, which depends on the
table staying sparse. It is sized at twice `max_active_orders`, so it never passes 50%
occupancy, and deletion shifts the run back rather than leaving a tombstone, so probe
runs do not grow over a session however many orders come and go.

The one step that is not constant is finding the next best price after a level empties.
The occupancy bitset makes it 64 levels per word, so a 1,000-tick gap is 16 words rather
than 1,000 checks. The alternative, a sorted container, would make every insert
O(log n) to make this one step cheaper; the gap is rare and inserts are not.

Starting a group is O(1) because of a generation counter: the journal keeps a generation
number per level and bumps one counter instead of clearing its arrays. A level whose
generation is stale reads as untouched. Clearing 16,384 entries per side on every
timestamp change would be the single most expensive thing the engine does.



## Allocation, capacity and error handling

The input file is loaded into memory once, before any timed region. After that the
protocol layer never allocates: `ByteReader` only holds a pointer and a length, the
decoded messages are plain structs on the stack, and the counters are fixed fields.

The order pool does allocate, but only in its constructor. It takes
`max_active_orders` slots and an index table of twice that, both sized once and never
grown. `std::vector` is used for them because it owns the memory safely, but
`push_back` is never called on either, so nothing moves and no pointer into the pool
goes stale. Per-message allocation is what the assignment rules out, not setup.

Errors are return values and counters, never exceptions. The assignment rules out
exceptions as normal control flow in the hot path, and a bad packet is normal here: it is
counted and skipped, not thrown about.

Each kind of problem is counted on its own, so the final report can say what actually
happened:

| Counter | Raised when |
|---|---|
| `invalid_packets` | header shorter than 20 bytes, block length past the end of the packet, blocks that do not add up to the datagram |
| `sequence_gaps` | a packet or heartbeat starts later than the next expected message |
| `duplicates` | a packet that was already seen, in full or in part |
| `invalid_messages` | a handled type with the wrong length, or a side byte that is neither `B` nor `S` |
| `skipped_types` | a message type this build does not handle |

A corrupt side byte is rejected in the parser rather than in the order book. `Side` is
used as an array index further down, so this is the last point where a bad value can be
stopped safely.

Two limits can be reached, and both stop the strategy instead of damaging anything:

- `max_active_orders` — the pool reports that it is full and the order is not stored.
  Nothing is overwritten and the orders already there are untouched.
- `max_price_levels` — a price outside the band is refused. If the order had already
  gone into the pool it is taken back out, so the pool and the levels never disagree.

Either one latches a flag the strategy checks. The book cannot be trusted after that
point, so it stops producing orders rather than trading on a book it knows is wrong.

## Sequence gap behaviour

MoldUDP64 numbers **messages**, not packets. A packet header gives the sequence number of
its first message and how many it carries, so the next expected number is
`sequence + count`.

Four cases are told apart. Say the next expected message is 8:

| Packet says | Case | What happens |
|---|---|---|
| first 8, count 3 | in order | all three are processed |
| first 12, count 1 | **gap** | 8 to 11 are lost; the gap is counted, the book is marked unreliable, and the packet is still processed |
| first 4, count 3 | **already seen** | the whole packet is dropped |
| first 6, count 4 | **partial overlap** | 6 and 7 are skipped, 8 and 9 are processed |

The last case matters. A retransmission can overlap what we already have. Processing all
of it would apply the same orders twice and corrupt the book. Dropping all of it would
lose the new messages.

Heartbeats carry the sequence number of the next message, so they can show a gap even
though they carry no data. That case is handled too.

**After a gap, the book stays marked unreliable.** The strategy will not produce new
orders while this flag is down. The flag only comes back up when the session changes,
because a new session is a new stream and the old gap no longer applies.

This is a decision, not something the assignment states. Missed messages cannot be
recovered without a snapshot feed such as GLIMPSE, so the conservative reading is that
the book never fully recovers inside the same session.

## Assumptions

These are the points where the protocol documents or the assignment left something open.

**`'T'` Seconds messages are tracked but never applied to the book.** ITCH splits the
timestamp: the second comes in its own message and every other message carries only
nanoseconds since that message. The nanosecond field therefore restarts every second.
Grouping on it alone would merge two messages a second apart into one group. The parser
keeps the current second and builds a 64-bit value instead:
`second * 1,000,000,000 + nanos`. The assignment lists four message types to handle and
`'T'` is not one of them, but watching a message and applying it to the book are two
different things.

**Messages that arrive before the first `'T'` are processed with second 0.** This only
happens if the capture starts in the middle of the stream. The absolute time is then
wrong, but ordering inside that stretch still works, so dropping the messages would lose
more than it saves. They are counted in `before_first_second`.

**`'C'` and `'Y'` are skipped, and on this feed that costs nothing.** The assignment says
to handle only four message types, but the spec's own "How to Build an Order Book View"
section also lists `'C'` (Order Executed with Price), which reduces an order's quantity,
and `'Y'` (Order Book Flush), which clears a book. Both were checked against the sample
feed:

- `'C'` does not appear at all. Not once, in 15.5 million messages.
- `'Y'` appears 3,897 times and touches book 73616 four times. None of them can change
  the result. The first lands while the book is still empty. The second shares its
  timestamp with `P_MARJ_YAYIN_KAPANIS`, by which point the strategy has already stopped.
  The other two come after the close.

So skipping them is safe here. It would not be safe in general, and a feed where `'C'`
was used, or where a flush landed during continuous trading, would need both.

`'U'` (Order Replace) is not used on BIST markets, so it costs nothing either.

**A session change resets sequence tracking and restores trust.** A different session
name means a different stream with its own numbering, so a low sequence number after a
change is not a duplicate, and a gap from the old stream no longer applies.

**A side byte that is not `B` or `S` makes the message invalid.** The message is counted
and dropped instead of being guessed at.

**`tick_size` is 10, not the 1 the assignment shows.** The assignment's example
config says 1, and with that value no spread is ever exactly two ticks and the
strategy never fires. Book 73616 trades on a 10-kurus grid: every pair of
adjacent prices in the feed is 10 apart, and the spread sits at 10 for 90.8% of
continuous trading and at 20 for 8.6%. That 20 is the single empty step. The
configuration supplied with the data later confirmed 10.

**`minimum_position` is 0, so nothing is sold short.** The sell size is
`position - minimum_position`, which with a floor of zero means only what is
already held can be sold. The first order of the day is therefore always a buy.
A side effect: every order comes out at the full 100 lots, because fills only
ever move the position in steps of 100, so the room left is either zero or at
least 100 and the partial sizing never triggers.

**A capacity of -1 switches that limit off, but something still has to be
allocated.** A fixed-capacity design cannot have no capacity, so -1 selects a
built-in default: 65,536 orders and 16,384 price levels. The feed's target book
peaked at 4,528 live orders across a 3,780 tick band, so both leave more than
ten times the room needed. Growing the structures instead was rejected: it
would put allocation back on the hot path, which is the one thing the
assignment rules out. Exceeding the default is still a safe error, because
there is no honest alternative, but it is no longer the configured limit being
reached.

**The day is valued when `P_MARJ_YAYIN_KAPANIS` arrives, not at end of file.**
The feed keeps trading after the close - closing auction, more executions - and
the last traded price drifts. On this day it was 19,160 at the close and 19,150
by the end of the file.

**Price uses the sentinel from the spec.** A Price field with bit 31 set and every other
bit zero (`INT32_MIN`) means "no price", and in an Add Order it means a market order. Such
an order has no price level and must not be placed on one.

**Packet framing in the file is one packet after another, confirmed.** Nothing is
specified about this anywhere, so it was checked directly: the file has no pcap or pcapng
magic number, and walking it as bare MoldUDP64 packets — 20 bytes of header plus the sum
of `2 + block length` over the blocks — lands exactly on the last byte after 519,492
packets, with no leftover and no block running past an end. It is still kept behind
`io/datagram_source.hpp` so a different feed with another layout is a one-file change.

Cash flow and P&L are stored in `int64_t`. The maximum term per fill is
`order_quantity * price`, so with the configured values (100 lots, an upper bound of
100,000 kurus) this comes to 10^7, leaving headroom of around 9x10^11 fills before
`INT64_MAX` — around 4 billion times the order count a typical session produces. If the
configuration gets more extreme (10^6 lots, a price near `INT32_MAX`) that headroom drops
to about 4,300 fills, so the multiplication and the accumulation are both checked with
`__builtin_mul_overflow` and `__builtin_add_overflow` in debug builds. `__int128` is not
used for the simple reason that it falls outside ISO C++11 and raises a warning under
`-Wpedantic`.

## Benchmark

```bash
taskset -c 4 ./orderbook --benchmark itch_data_250815_HI2.dat --iterations 50 --config config.txt
```

Five rules decide what the number means:

1. The file is in memory before anything is timed.
2. One warm-up pass runs unmeasured and is thrown away.
3. The data is replayed many times, and each pass builds a fresh pipeline.
4. No file or console output happens inside the timed region.
5. Each pass produces a checksum, so `-O3` cannot delete the work as dead.

The allocation counter is reset after the pipeline is built and read when the pass ends,
so it reports what the hot path asked for rather than what setup did.

Full environment, method and results: [benchmark_report.md](benchmark_report.md).

## Tests

```
make check
```

The sample market data had not arrived when work started, so the test helpers build
synthetic input instead. `tests/message_builder.hpp` writes real MoldUDP64 packets and
the four ITCH messages, and it can also break one thing at a time: cut a header short,
set a block length that runs past the packet, or claim more messages than the packet
carries. The protocol tests are built on that.

Both sides of the wire format are tested together. The builder writes bytes and
`ByteReader` reads them back, and a round-trip test compares every field. Written
separately, the two could still agree on a wrong offset and both look correct on their
own; only the round trip catches that.

216 tests, in two layers. Most of them check one component against its own interface.
On top of those, `test_protocol.cpp`, `test_strategy.cpp` and `test_pnl.cpp` build a
feed out of wire bytes and run it through the whole assembled program, reading the
result out of the reported summary. A component can be correct on its own and still be
wired up wrongly: a counter read from the wrong object, a handler never reached, a limit
applied to the wrong number. Only a run through the whole chain catches that.

The scenarios the assignment asks for, and where each one is checked end to end:

| Scenario | Test |
|---|---|
| Truncated header | `protocol_a_truncated_header_stops_the_walk_and_is_reported` |
| Invalid message length | `protocol_a_message_shorter_than_its_type_is_counted_and_skipped` |
| Sequence gap | `protocol_a_sequence_gap_is_counted_and_silences_the_strategy` |
| Heartbeat | `protocol_a_heartbeat_carries_nothing_and_breaks_nothing` |
| End of session | `protocol_end_of_session_is_a_bare_header_and_frames_correctly` |
| Non-target order book | `protocol_another_order_book_never_reaches_the_engine` |
| Transient gap in one group | `strategy_e2e_a_gap_that_opens_and_closes_in_one_group_is_not_a_signal` |
| Real BUY gap | `strategy_e2e_a_bid_that_vanishes_produces_one_buy` |
| Real SELL gap | `strategy_e2e_an_ask_that_vanishes_produces_one_sell` |
| Repeated signal suppressed | `strategy_e2e_one_order_per_gap_however_long_it_stays_open` |
| Quantity cut near the limit | `pnl_e2e_the_quantity_is_cut_down_to_what_the_limit_allows` |
| No order at the limit | `pnl_e2e_nothing_is_sent_when_there_is_no_room_left` |
| BUY cash flow | `pnl_e2e_a_buy_pays_out_and_goes_long` |
| SELL cash flow | `pnl_e2e_a_sell_takes_in_cash_and_goes_short` |
| End of day mark to market | `pnl_e2e_the_day_is_marked_at_the_closing_price` |

Each of these is also checked against the component that implements it, in
`test_mold_udp64.cpp`, `test_itch_parser.cpp`, `test_gap_strategy.cpp` and
`test_position.cpp`.

## The sample feed

`itch_data_250815_HI2.dat`, 584 MB, 15 August 2025. One session, `TR29253481`.

| | |
|---|---|
| Packets | 519,492 |
| ITCH messages | 15,560,041 |
| Order books in the file | 1,769 |
| Sequence gaps / duplicates | 0 / 0 |
| Target book 73616 | `EKGYO.E` |
| Target messages (A / E / D) | 147,637 / 54,409 / 106,146 |
| Peak live orders in the target book | 4,528 |
| Price range | 17,060 to 20,840 kurus (3,780 ticks) |
| Timestamp groups in the target book | 197,449 |

Message types present: `A D E Z P T O L Y R V S`. Four of them are handled, the rest are
skipped.

The day runs `P_SUREKLI_ISLEM` to `P_MARJ_YAYIN_KAPANIS`, exactly eight hours apart, and
each state appears once. Most timestamp groups are small — 61% hold a single message and
37% hold two — but the largest holds 3,919, which is why the strategy must never run in
the middle of one.

The feed also carries no orders with a zero (undisclosed) quantity and no market-order
price sentinel in the target book. Both are still handled, since neither is ruled out by
the protocol.

The capacities in `config.example` are sized from these numbers with more than 10x
headroom.

## Known limitations

**`'C'` and `'Y'` are not handled.** Verified harmless on this feed, as explained under
Assumptions, but a feed that used `'C'`, or flushed a book during continuous trading,
would need both. `'C'` reduces a quantity like `'E'` does, and `'Y'` clears the book, so
neither is hard to add.

**The fill model is optimistic, and the P&L should be read that way.** Every
generated order is treated as filled in full at its own price. In a real market
placing an order into a gap buys you queue position, not a trade: some orders
never fill, and the ones that do are disproportionately the ones filled by
whoever is pushing the price away from you. The number this produces is closer
to an upper bound than to an estimate.

**Only one day of data has been checked.** The capacity numbers above come from a single
session. A busier day would need larger limits, which is why they are configuration
rather than constants.

**A gap disables the strategy for the rest of the session.** With a snapshot feed the
book could be rebuilt and trading could continue, but ITCH alone cannot do that.

**Latency numbers come from inside a VM.** Everything was measured under WSL2. The
median and the useful percentiles are stable, but the far tail belongs to the
hypervisor, so a bare-metal run would look different past p99.9.
