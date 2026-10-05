# Benchmark Report

## Environment

| | |
|---|---|
| CPU | |
| Cores / frequency | |
| CPU pinning | |
| OS / kernel | |
| Compiler | GCC 8.5.0 |
| Flags | `-O3 -DNDEBUG -std=c++11` |
| Timer | |
| Warm-up | |
| Iterations | |

## Method

<!-- how the input was loaded, what the timed region covers, how latency was
     sampled, how the checksum prevents elimination -->

## Results

| Category | Metric | Value |
|---|---|---|
| Volume | Packets | |
| | ITCH messages | |
| | Target messages | |
| | Order intents | |
| Throughput | Total time | |
| | Messages/second | |
| | Average ns/message | |
| Latency | p50 | |
| | p90 | |
| | p99 | |
| | p99.9 | |
| | Maximum | |
| Resources | Hot-path allocations | |
| | Peak memory | |
| Validation | Checksum | |

## Analysis

<!-- where the time goes, what the tail looks like and why, which decisions
     moved the numbers -->
