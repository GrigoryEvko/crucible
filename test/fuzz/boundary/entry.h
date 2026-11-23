#pragma once

// The two functions that each harness exports from its own translation unit.
//
// Two harness headers can include headers that do not compile together.  So
// CMake compiles each harness alone, once, into an object library from
// harness_entry.cpp.  The fuzz binary of the harness and the property test
// that runs every harness link that object and call the functions declared
// here.  The two functions hold only types of the base: run_<name> takes the
// View of the bytes of one input, and seeds_<name> gives the seeds of the
// harness as a SeedList.

#include <fixy/Core.h>
#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

namespace crucible::fuzz::boundary {

// The bytes of one seed.
using SeedBytes = ::fixy::HeapArray<std::byte>;

// The seeds of a harness, in the order of the harness.
using SeedList = ::foundation::core::GrowList<SeedBytes>;

// The largest count of the seeds of one list: the seeds of a harness and each
// file of a corpus directory that the runner reads.
inline constexpr std::size_t kMaxSeeds = std::size_t{1} << 20;

// The allocation capability of the test context, for each seed, each loader
// and each arena that a harness drives.
[[nodiscard]] constexpr auto test_alloc() noexcept { return ::foundation::effects::testing::test().alloc; }

// An empty list of seeds.
[[nodiscard]] inline SeedList empty_seed_list() noexcept {
    return ::foundation::core::mint_grow_list<SeedBytes>(kMaxSeeds);
}

// The bytes of bytes, in a seed of their own.  Complexity: one copy of each
// byte.
[[nodiscard]] inline SeedBytes copy_seed(::fixy::View<std::byte const> bytes) noexcept {
    SeedBytes seed = ::fixy::mint_heap_array_zeroed<std::byte>(test_alloc(), bytes.size());
    seed = ::fixy::with_mut(std::move(seed), [bytes](auto run) noexcept {
        ::fixy::copy(std::move(run), bytes).expect("a seed holds each byte of its source");
    });
    return seed;
}

// Puts seed after the last seed of seeds.
inline void add_seed(SeedList& seeds, SeedBytes seed) noexcept {
    seeds.append(test_alloc(), std::move(seed)).expect("a list holds at most kMaxSeeds seeds");
}

// The SeedList of the seeds of a harness.  A harness gives its seeds in a
// container of its own, such as a FixedList of images, and each seed gives the
// View of its bytes.  Complexity: one copy of each byte of each seed.
template <class Seeds>
[[nodiscard]] SeedList seed_list_of(Seeds const& seeds) noexcept {
    SeedList list = empty_seed_list();
    for (auto const& seed : seeds) {
        add_seed(list, copy_seed(::fixy::View<std::byte const>{seed.view()}));
    }
    return list;
}

// A harness that gives a SeedList gives it with no copy.
[[nodiscard]] inline SeedList seed_list_of(SeedList&& seeds) noexcept { return std::move(seeds); }

}  // namespace crucible::fuzz::boundary

#define CRUCIBLE_FUZZ_DECLARE_HARNESS(name)                                     \
    namespace crucible::fuzz::boundary::entry {                                 \
    void run_##name(::fixy::View<std::byte const> input) noexcept;              \
    [[nodiscard]] ::crucible::fuzz::boundary::SeedList seeds_##name() noexcept; \
    }

// The indirections expand a macro argument, such as
// CRUCIBLE_FUZZ_HARNESS_NAME, before ## pastes it.
#define CRUCIBLE_FUZZ_DECLARE_HARNESS_OF(name) CRUCIBLE_FUZZ_DECLARE_HARNESS(name)
#define CRUCIBLE_FUZZ_ENTRY_RUN_(name) ::crucible::fuzz::boundary::entry::run_##name
#define CRUCIBLE_FUZZ_ENTRY_RUN(name) CRUCIBLE_FUZZ_ENTRY_RUN_(name)
#define CRUCIBLE_FUZZ_ENTRY_SEEDS_(name) ::crucible::fuzz::boundary::entry::seeds_##name
#define CRUCIBLE_FUZZ_ENTRY_SEEDS(name) CRUCIBLE_FUZZ_ENTRY_SEEDS_(name)
