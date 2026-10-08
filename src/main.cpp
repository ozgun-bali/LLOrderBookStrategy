// CLI entry point.
//
//   ./orderbook --input sample.bin --config config.txt [--orders orders.csv]
//   ./orderbook --benchmark sample.bin --iterations 100 [--config config.txt]
//
// The input file is read into memory once, before anything that will later be
// timed. Everything downstream works on views into that one buffer: no copy is
// made of a packet, a message or a field.

#include "app/benchmark.hpp"
#include "app/pipeline.hpp"
#include "app/reporter.hpp"
#include "core/config.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace llob;

namespace {

bool readWholeFile(const char* path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;

    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);

    out.resize((std::size_t)size);
    if (out.empty()) return true;
    return (bool)f.read((char*)&out[0], size);
}

void usage() {
    std::fprintf(stderr,
        "usage:\n"
        "  orderbook --input <file> --config <file> [--orders <file.csv>]\n"
        "  orderbook --benchmark <file> --iterations <n> [--config <file>]\n");
}

void printBenchmark(std::ostream& out, const BenchmarkResult& r) {
    out << "=== volume ===\n";
    out << "packets                 = " << r.packets_per_iteration  << "\n";
    out << "itch_messages           = " << r.messages_per_iteration << "\n";
    out << "target_messages         = " << r.target_messages        << "\n";
    out << "order_intents           = " << r.order_intents          << "\n";

    char buf[128];
    out << "\n=== throughput ===\n";
    out << "iterations              = " << r.iterations << "\n";
    std::snprintf(buf, sizeof(buf), "%.3f", r.total_seconds);
    out << "total_time_s            = " << buf << "\n";
    std::snprintf(buf, sizeof(buf), "%.3f", r.best_seconds);
    out << "best_pass_s             = " << buf << "\n";
    std::snprintf(buf, sizeof(buf), "%.3f", r.worst_seconds);
    out << "worst_pass_s            = " << buf << "\n";
    std::snprintf(buf, sizeof(buf), "%.2f", r.messages_per_second / 1e6);
    out << "messages_per_second     = " << buf << " M\n";
    std::snprintf(buf, sizeof(buf), "%.1f", r.ns_per_message);
    out << "avg_ns_per_message      = " << buf << "\n";

    out << "\n=== latency, per datagram ===\n";
    out << "samples                 = " << r.latency.samples << "\n";
    out << "sampled_every           = " << r.sample_every << " datagrams\n";
    out << "p50                     = " << r.latency.p50  << " ns\n";
    out << "p90                     = " << r.latency.p90  << " ns\n";
    out << "p99                     = " << r.latency.p99  << " ns\n";
    out << "p99.9                   = " << r.latency.p999 << " ns\n";
    out << "max                     = " << r.latency.max  << " ns\n";
    out << "mean                    = " << r.latency.mean << " ns\n";
    out << "timer_overhead          = " << r.timer_overhead_ns
        << " ns (included above)\n";
    if (r.latency.overflow)
        out << "over_4us                = " << r.latency.overflow << " samples\n";
    if (r.latency.saturated)
        out << "note                    = a percentile fell past the histogram\n";

    out << "\n=== resources ===\n";
    out << "hot_path_allocations    = " << r.hot_path_allocations << "\n";
    out << "hot_path_bytes          = " << r.hot_path_bytes << "\n";
    out << "peak_memory             = " << r.peak_memory_kb << " kB\n";

    out << "\n=== validation ===\n";
    std::snprintf(buf, sizeof(buf), "0x%016llX", (unsigned long long)r.checksum);
    out << "checksum                = " << buf << "\n";
    out << "checksum_stable         = "
        << (r.checksum_stable ? "yes, every iteration agreed" : "NO") << "\n";
}

} // namespace

int main(int argc, char** argv) {
    const char* inputPath  = 0;
    const char* benchPath  = 0;
    const char* configPath = "config.txt";
    const char* ordersPath = "orders.csv";
    uint64_t    iterations = 10;

    for (int i = 1; i < argc; ++i) {
        const bool hasNext = (i + 1 < argc);
        if      (!std::strcmp(argv[i], "--input")      && hasNext) inputPath  = argv[++i];
        else if (!std::strcmp(argv[i], "--benchmark")  && hasNext) benchPath  = argv[++i];
        else if (!std::strcmp(argv[i], "--config")     && hasNext) configPath = argv[++i];
        else if (!std::strcmp(argv[i], "--orders")     && hasNext) ordersPath = argv[++i];
        else if (!std::strcmp(argv[i], "--iterations") && hasNext)
            iterations = (uint64_t)std::strtoull(argv[++i], 0, 10);
        else { usage(); return 2; }
    }

    if ((inputPath && benchPath) || (!inputPath && !benchPath)) { usage(); return 2; }
    if (benchPath && iterations == 0) {
        std::fprintf(stderr, "--iterations must be at least 1\n");
        return 2;
    }

    Config      cfg;
    std::string error;
    if (!load_config(configPath, cfg, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    const char* path = inputPath ? inputPath : benchPath;
    std::vector<uint8_t> data;
    if (!readWholeFile(path, data)) {
        std::fprintf(stderr, "cannot read '%s'\n", path);
        return 1;
    }
    if (data.empty()) {
        std::fprintf(stderr, "'%s' is empty\n", path);
        return 1;
    }

    if (benchPath) {
        const BenchmarkResult r = run_benchmark(cfg, &data[0], data.size(), iterations);
        printBenchmark(std::cout, r);
        return 0;
    }

    OrderLog log;
    Pipeline pipeline(cfg, log);
    pipeline.run(&data[0], data.size());

    const RunSummary s = pipeline.summary(log);
    print_summary(std::cout, s);

    if (pipeline.truncated())
        std::fprintf(stderr, "warning: input ends mid-packet at byte %zu\n",
                     pipeline.offset());
    if (log.grew())
        std::fprintf(stderr, "warning: order log outgrew its reserve\n");

    if (!write_order_csv(ordersPath, log)) {
        std::fprintf(stderr, "cannot write '%s'\n", ordersPath);
        return 1;
    }
    return 0;
}
