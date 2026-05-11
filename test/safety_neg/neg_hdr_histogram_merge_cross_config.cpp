#include <crucible/observe/HdrHistogram.h>

int main() {
    crucible::observe::HdrHistogram<2, 1'000'000> low_precision;
    crucible::observe::HdrHistogram<3, 1'000'000> high_precision;

    // GAPS-135: merge/subtract require identical bucket geometry; a
    // cross-config merge would corrupt percentile meaning.
    low_precision.merge_from(high_precision);
}
