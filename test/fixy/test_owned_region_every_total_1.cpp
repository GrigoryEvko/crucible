// Every total from 0 to 64 into each shard count from 10 to 13.

#include "owned_region.h"

#include <cstddef>
#include <utility>

namespace test_owned_region {

void split_and_recombine_every_total_of_counts_10_to_13() {
    []<std::size_t... Counts>(std::index_sequence<Counts...>) {
        (split_and_recombine_every_total<Counts + 10>(), ...);
    }(std::make_index_sequence<4>{});
}

}  // namespace test_owned_region
