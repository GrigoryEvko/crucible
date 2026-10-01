// Every total from 0 to 64 into each shard count from 14 to 16.

#include "owned_region.h"

#include <cstddef>
#include <utility>

namespace test_owned_region {

void split_and_recombine_every_total_of_counts_14_to_16() {
    []<std::size_t... Counts>(std::index_sequence<Counts...>) {
        (split_and_recombine_every_total<Counts + 14>(), ...);
    }(std::make_index_sequence<3>{});
}

}  // namespace test_owned_region
