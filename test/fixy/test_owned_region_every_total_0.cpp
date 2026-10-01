// Every total from 0 to 64 into each shard count from 1 to 9.

#include "owned_region.h"

#include <cstddef>
#include <utility>

namespace test_owned_region {

void split_and_recombine_every_total_of_counts_1_to_9() {
    []<std::size_t... Counts>(std::index_sequence<Counts...>) {
        (split_and_recombine_every_total<Counts + 1>(), ...);
    }(std::make_index_sequence<9>{});
}

}  // namespace test_owned_region
