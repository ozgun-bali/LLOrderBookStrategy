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
./orderbook --input sample.bin --config config.txt
./orderbook --benchmark sample.bin --iterations 100
```

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

<!-- order storage, price level representation and best price maintenance go here
     once the order book is written -->

## Complexity

| Step | Time | Space |
|---|---|---|
| Read one field | O(1) | none |
| Frame one packet | O(blocks) | none |
| Dispatch one message | O(1) | one small struct on the stack |
| Filter a non-target message | O(1), two reads | none |

Nothing in the protocol layer scans or sorts. A packet costs one pass over its blocks,
and a message costs a fixed number of reads.

<!-- order book and strategy complexity go here -->

## Allocation, capacity and error handling

The input file is loaded into memory once, before any timed region. After that the
protocol layer never allocates: `ByteReader` only holds a pointer and a length, the
decoded messages are plain structs on the stack, and the counters are fixed fields.
`std::vector` appears only in the test helpers, never in `include/` or `src/`.

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

<!-- capacity limits (max_active_orders, max_price_levels) go here once the pool
     and the level array are written -->

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

**`'C'` and `'Y'` are skipped, and this has a cost.** The assignment says to handle only
four message types. The spec's own "How to Build an Order Book View" section also lists
`'C'` (Order Executed with Price), which reduces an order's quantity, and `'Y'` (Order
Book Flush), which clears the book. Skipping them can leave quantities too high or a book
that should have been emptied. `'U'` (Order Replace) is not used on BIST markets, so it
costs nothing.

**A session change resets sequence tracking and restores trust.** A different session
name means a different stream with its own numbering, so a low sequence number after a
change is not a duplicate, and a gap from the old stream no longer applies.

**A side byte that is not `B` or `S` makes the message invalid.** The message is counted
and dropped instead of being guessed at.

**Price uses the sentinel from the spec.** A Price field with bit 31 set and every other
bit zero (`INT32_MIN`) means "no price", and in an Add Order it means a market order. Such
an order has no price level and must not be placed on one.

**How packets are framed inside the input file is not specified anywhere.** The most
likely layout is one packet after another with nothing between them, because MoldUDP64
is self-delimiting: a packet is 20 bytes plus the sum of `2 + block length` over its
blocks. This is kept behind `io/datagram_source.hpp` so that switching to pcap or
length-prefixed records is a one-file change. This will be confirmed against the sample
data once it arrives.

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

See [benchmark_report.md](benchmark_report.md).

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

## Known limitations

**The sample data has not been checked yet.** Everything so far is tested against
synthetic input. The framing inside the file still needs to be confirmed.

**`'C'` and `'Y'` are not handled**, as explained under Assumptions. With more time these
would be easy to add: `'C'` reduces a quantity like `'E'`, and `'Y'` clears the book.

**A gap disables the strategy for the rest of the session.** With a snapshot feed the
book could be rebuilt and trading could continue, but ITCH alone cannot do that.

**Order book, strategy, fill model and P&L are still to be written.** The protocol layer
below them is complete and tested.
