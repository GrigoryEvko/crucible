#include <crucible/observe/HdrHistogram.h>

int main() {
    crucible::observe::HdrHistogram<2, 1000000> low_precision;
    crucible::observe::HdrHistogram<3, 1000000> high_precision;

    // Merge and subtract require identical bucket geometry: a cross-config
    // merge would corrupt the meaning of every percentile.
    low_precision.merge_from(high_precision);
}
