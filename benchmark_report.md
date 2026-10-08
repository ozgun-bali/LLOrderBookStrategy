# Benchmark Report

`itch_data_250815_HI2.dat`, 584,628,619 bytes, book 73616 (`EKGYO.E`),
with the configuration supplied alongside it.

## Environment

|                      |                                                                  |
| -------------------- | ---------------------------------------------------------------- |
| CPU                  | AMD Ryzen 7 7800X3D, 8 cores / 16 threads                        |
| TSC rate             | 4.2000 GHz (measured by calibration)                             |
| CPU pinning          | `taskset -c 4`, one core for the whole run                       |
| OS                   | AlmaLinux 8.10 on WSL2, kernel 6.18.40.1-microsoft-standard-WSL2 |
| Compiler             | GCC 8.5.0                                                        |
| Flags                | `-std=c++11 -O3 -DNDEBUG -Wall -Wextra -Wpedantic`               |
| Timing, whole pass   | `std::chrono::steady_clock`                                      |
| Timing, per datagram | `rdtsc`, 0.238 ns per cycle                                      |
| Warm-up              | one full unmeasured pass before the first measurement            |
| Iterations           | 50                                                               |

**This is a virtual machine.** WSL2 runs under a Hyper-V hypervisor, so the
Windows host can deschedule the whole guest regardless of pinning. That does
not touch the median or the useful percentiles, but it owns the maximum. See
the note under Latency.

## Method

1. The input file is read into memory before anything is timed. Timing it
   would measure the 9p filesystem between WSL and the host, which took about
   2.9 seconds against 0.10 seconds of actual work.
2. One warm-up pass runs unmeasured and is discarded. A cold first pass faults
   in 584 MB of freshly allocated pages one at a time and starts with empty
   caches and branch predictors; none of that is the code's behaviour.
3. Fifty measured passes follow. Each builds a fresh pipeline, so every pass
   starts from an empty book and does identical work.
4. No file or console output happens inside the timed region.
5. Each pass produces a checksum. Without a value leaving the program, `-O3`
   would be entitled to delete the whole computation as dead.

The allocation counter is reset after the pipeline is constructed and read
after the pass, so it reports what the hot path asked for rather than what
setup did.

### What the two latency numbers measure

**Average ns per message** comes from the whole pass divided by the message
count. It carries no per-message instrumentation at all.

**Per-datagram percentiles** come from timing `process_datagram` on every
eighth datagram. That call is the interface the assignment specifies, and the
unit is meaningful here because the feed is almost perfectly uniform: 510,703
of 519,492 packets carry exactly 30 messages, and none carries more.

Timing every datagram was avoided on purpose. `steady_clock` costs tens of
nanoseconds against a median datagram of 119 ns, so the instrument would have
been a large part of the measurement. `rdtsc` is cheap enough at 0.238 ns that
sampling is almost a formality, but one in eight keeps even that out of the
way.

The sampled region covers `process_datagram` only. Finding the packet boundary
is excluded, because a live feed gets that from UDP for free and only a file
needs it recovered. That is why the throughput figure per datagram (195 ns) is
above the sampled mean (154 ns).

## Results

| Category              | Metric                      | Value                |
| --------------------- | --------------------------- | -------------------- |
| Volume                | Packets                     | 519,492              |
|                       | ITCH messages               | 15,560,041           |
|                       | Target messages             | 308,209              |
|                       | Order intents               | 1,491                |
| Throughput            | Total time, 50 passes       | 5.277 s              |
|                       | Best pass                   | 0.101 s              |
|                       | Worst pass                  | 0.144 s              |
|                       | Messages per second         | 154.4 M              |
|                       | Average ns per message      | 6.5                  |
| Latency, per datagram | p50                         | 119 ns               |
|                       | p90                         | 209 ns               |
|                       | p99                         | 459 ns               |
|                       | p99.9                       | 1,039 ns             |
|                       | Maximum                     | 8,306,725 ns         |
|                       | Mean                        | 154 ns               |
|                       | Samples                     | 3,246,800            |
| Resources             | Hot-path allocations        | **0**                |
|                       | Hot-path bytes              | **0**                |
|                       | Peak memory                 | 577,420 kB           |
| Validation            | Checksum                    | `0x45F146A704E0878C` |
|                       | Stable across all 50 passes | yes                  |

Per message, dividing the datagram figures by 30: about 4 ns at the median and
35 ns at p99.9.

## Reading the numbers

**Zero allocations is measured, not claimed.** The global `operator new` is
replaced, so every allocation in the program and in anything it links against
is counted. After construction the counter is reset; after the pass it reads
zero. Nothing on the hot path asks the allocator for anything.

**The maximum belongs to the hypervisor, not the code.** 8.3 ms for one
datagram is four orders of magnitude past p99.9, and the packets are uniform
so there is no large datagram to blame. It is the guest being descheduled.
978 samples out of 3,246,800 exceed 4 µs, which is 0.03%. On bare metal the
tail past p99.9 would be worth taking seriously; here it is mostly a property
of the room.

**p99.9 against p50 is the number that matters.** 1,039 ns against 119 ns is
roughly nine times, and that is where the design shows. The occupancy bitset
keeps the best-price search from degrading into a scan when a level empties
with a gap above it, and the backward-shift deletion keeps hash probes short
no matter how many orders have come and gone. Both of those were chosen for
the tail rather than the average.

**Throughput is dominated by messages that are thrown away.** 15.56 M messages
arrive and 308 k belong to the target book, so 98% are rejected by reading the
type byte, checking the length and comparing one 32-bit field. At 6.5 ns per
message on average, that rejection path is what is being measured most of the
time. The work on a target message is considerably more than 6.5 ns; the
average is low because most messages never get that far.

**Peak memory is the input file.** 577 MB resident against 557 MiB of file.
The order pool, the price levels and the journal together are a few megabytes:
65,536 order slots at 24 bytes, two index tables, 16,384 levels per side at 16
bytes, and the journal's three arrays.

## What is not measured here

Per-message latency distribution. Getting it would mean a timer around each
message, and at roughly 4 ns of work per message the timer would cost more
than the thing it measures. The per-datagram distribution plus the uniform
packet size is the closest honest substitute.

Behaviour under a feed with sequence gaps. This file has none, so the
reliability path never fires and costs nothing here.

A bare-metal run. Everything above is from inside a VM, and the tail would
look different outside one.
