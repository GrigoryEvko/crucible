#include <crucible/observe/HdrHistogram.h>

#include <cstdint>

int main() {
    using Hist = crucible::observe::HdrHistogram<2, 1'000'000>;
    Hist h;
    std::uint64_t raw = 10;

    // GAPS-135: record() accepts only the Refined in-range value type.
    // A raw external latency sample must be validated at the boundary.
    h.record(raw);
}
