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

<!-- component responsibilities and the data flow between them -->

## Data structures and why

<!-- order storage, price level representation, best price maintenance,
     what was considered and rejected -->

## Complexity

<!-- time and space, per message type -->

## Allocation, capacity and error handling

<!-- what is preallocated, what happens at capacity, where (if anywhere)
     the hot path still allocates and why -->

## Sequence gap behaviour

<!-- what is detected, what is suppressed, whether and how it recovers -->

## Assumptions

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

## Known limitations

<!-- and what would be done with more time -->
