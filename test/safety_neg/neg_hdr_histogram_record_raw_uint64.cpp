#include <crucible/observe/HdrHistogram.h>

#include <cstdint>

int main() {
    using Hist = crucible::observe::HdrHistogram<2, 1000000>;
    Hist h;
    std::uint64_t raw = 10;

    // record() accepts only the refined in-range value type.  A raw
    // external latency sample passes checked_value() at the boundary.
    h.record(raw);
}
