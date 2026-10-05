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

<!-- every point where the protocol docs or the assignment were unclear.
     See the working notes for the running list. -->

## Benchmark

See [benchmark_report.md](benchmark_report.md).

## Known limitations

<!-- and what would be done with more time -->
