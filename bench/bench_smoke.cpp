// Smoke test for benchmark harness.
#include "bench_harness.h"

int main() {
    std::printf("=== Crucible Benchmark Smoke Test ===\n\n");

    volatile int sink = 0;
    BENCH_CHECK("noop baseline", 10'000'000, 0.5, {
        sink = sink + 1;
        bench::DoNotOptimize(sink);
    });

    std::printf("\nDone.\n");
    return 0;
}
