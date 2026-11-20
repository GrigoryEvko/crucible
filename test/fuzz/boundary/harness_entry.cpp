// The translation unit of one harness.
//
// CMake compiles this file once per harness, with CRUCIBLE_FUZZ_HARNESS_NAME
// set to the harness name and CRUCIBLE_FUZZ_HARNESS_HEADER set to its header
// under harnesses/.  The header defines run_<name>(View<std::byte const>) and
// seeds_<name>() in crucible::fuzz::boundary.  seeds_<name>() gives the seeds
// in a container of the harness, and each seed gives the View of its bytes.
// This file exports the two functions under the names that entry.h declares,
// with the seeds as a SeedList.

#include "entry.h"

#include CRUCIBLE_FUZZ_HARNESS_HEADER

CRUCIBLE_FUZZ_DECLARE_HARNESS_OF(CRUCIBLE_FUZZ_HARNESS_NAME)

#define CRUCIBLE_FUZZ_DEFINE_ENTRY_(name)                                                            \
    void crucible::fuzz::boundary::entry::run_##name(::fixy::View<std::byte const> input) noexcept { \
        ::crucible::fuzz::boundary::run_##name(input);                                               \
    }                                                                                                \
    ::crucible::fuzz::boundary::SeedList crucible::fuzz::boundary::entry::seeds_##name() noexcept {  \
        return ::crucible::fuzz::boundary::seed_list_of(::crucible::fuzz::boundary::seeds_##name()); \
    }
#define CRUCIBLE_FUZZ_DEFINE_ENTRY(name) CRUCIBLE_FUZZ_DEFINE_ENTRY_(name)

CRUCIBLE_FUZZ_DEFINE_ENTRY(CRUCIBLE_FUZZ_HARNESS_NAME)
