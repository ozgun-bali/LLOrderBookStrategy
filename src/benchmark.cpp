// Benchmark harness.
//
//   1. input already in memory before the timed region
//   2. at least one warm-up iteration
//   3. replay the same data --iterations times
//   4. no file or console I/O inside the timed region
//   5. emit a checksum so -O3 cannot eliminate the work
//
// Report: packets, ITCH messages, target messages, order intents, total time,
// messages/second, average ns/message, p50 / p90 / p99 / p99.9 / max latency,
// hot-path allocation count, peak memory, checksum.
//
// Timing every single message distorts the measurement. Prefer total throughput
// plus sampled or batched latency, and state which was used. Record the timer,
// warm-up method, CPU pinning and environment in benchmark_report.md.

#include "app/stats.hpp"

// TODO
