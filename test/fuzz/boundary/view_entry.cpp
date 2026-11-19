// The translation unit of one harness whose interface holds only types of the
// base.
//
// Such a harness defines run_<name>(View<std::byte const>) and seeds_<name>()
// in crucible::fuzz::boundary.  seeds_<name>() gives a list of seeds, and
// each seed gives the View of its bytes through view().  The fuzz entry of
// fuzz_main.cpp, afl-fuzz and the runner of runner_main.h pass each input and
// each seed as a run of raw bytes of the C++ library.  So this file holds the
// one edge between the two forms, in one ABI region, and the harness holds no
// type of the C++ library.  CMake compiles it in place of harness_entry.cpp
// for each harness of the form, and it exports the functions that entry.h
// declares.

#include "entry.h"

#include CRUCIBLE_FUZZ_HARNESS_HEADER

#include <foundation/Quarantine.h>
#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fuzzer gives its input as a pointer to raw bytes and a count")

// The names of the harness and of its exports.  The indirections expand
// CRUCIBLE_FUZZ_HARNESS_NAME before ## pastes it.  The macros stand in the
// region, because a finding of a name takes the line of its spelling.
#define CRUCIBLE_FUZZ_VIEW_RUN_(name) ::crucible::fuzz::boundary::run_##name
#define CRUCIBLE_FUZZ_VIEW_RUN(name) CRUCIBLE_FUZZ_VIEW_RUN_(name)
#define CRUCIBLE_FUZZ_VIEW_SEEDS_(name) ::crucible::fuzz::boundary::seeds_##name
#define CRUCIBLE_FUZZ_VIEW_SEEDS(name) CRUCIBLE_FUZZ_VIEW_SEEDS_(name)
#define CRUCIBLE_FUZZ_EXPORT_RUN_(name) crucible::fuzz::boundary::entry::run_##name
#define CRUCIBLE_FUZZ_EXPORT_RUN(name) CRUCIBLE_FUZZ_EXPORT_RUN_(name)
#define CRUCIBLE_FUZZ_EXPORT_SEEDS_(name) crucible::fuzz::boundary::entry::seeds_##name
#define CRUCIBLE_FUZZ_EXPORT_SEEDS(name) CRUCIBLE_FUZZ_EXPORT_SEEDS_(name)

CRUCIBLE_FUZZ_DECLARE_HARNESS_OF(CRUCIBLE_FUZZ_HARNESS_NAME)

// The input lives for the call, so its View is valid in the harness.
void CRUCIBLE_FUZZ_EXPORT_RUN(CRUCIBLE_FUZZ_HARNESS_NAME)(std::span<const std::uint8_t> bytes) {
    CRUCIBLE_FUZZ_VIEW_RUN(CRUCIBLE_FUZZ_HARNESS_NAME)
    (::foundation::core::byte_view(::foundation::core::mint_foreign_view(bytes.data(), bytes.size())));
}

// Each seed of the harness, as the runner takes it.  Complexity: one copy of
// each byte of each seed.
std::vector<std::vector<std::uint8_t>> CRUCIBLE_FUZZ_EXPORT_SEEDS(CRUCIBLE_FUZZ_HARNESS_NAME)() {
    std::vector<std::vector<std::uint8_t>> seeds;
    for (auto const& seed : CRUCIBLE_FUZZ_VIEW_SEEDS(CRUCIBLE_FUZZ_HARNESS_NAME)()) {
        std::vector<std::uint8_t>& raw = seeds.emplace_back();
        for (std::byte const byte : seed.view()) {
            raw.push_back(static_cast<std::uint8_t>(byte));
        }
    }
    return seeds;
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
