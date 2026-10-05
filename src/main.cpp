// CLI entry point and mode dispatch.
//
//   ./orderbook --input sample.bin --config config.txt
//   ./orderbook --benchmark sample.bin --iterations 100
//
// Reads the input file into memory once, before anything timed. Builds the
// engine, feeds datagrams, finalizes, then hands results to the reporter.
// Cold path - allocation here is fine.

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    return 0;
}
