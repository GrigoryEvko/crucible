#pragma once

// The two functions that each harness exports from its own translation unit.
//
// Two harness headers can include headers that do not compile together.  So
// CMake compiles each harness alone, once, into an object library from
// harness_entry.cpp.  The
// fuzz binary of the harness and the property test that runs every harness
// link that object and call the functions declared here.

#include <cstdint>
#include <span>
#include <vector>

#define CRUCIBLE_FUZZ_DECLARE_HARNESS(name)                              \
    namespace crucible::fuzz::boundary::entry {                         \
    void run_##name(std::span<const std::uint8_t> bytes);               \
    [[nodiscard]] std::vector<std::vector<std::uint8_t>> seeds_##name(); \
    }

// The indirections expand a macro argument, such as
// CRUCIBLE_FUZZ_HARNESS_NAME, before ## pastes it.
#define CRUCIBLE_FUZZ_DECLARE_HARNESS_OF(name) CRUCIBLE_FUZZ_DECLARE_HARNESS(name)
#define CRUCIBLE_FUZZ_ENTRY_RUN_(name) ::crucible::fuzz::boundary::entry::run_##name
#define CRUCIBLE_FUZZ_ENTRY_RUN(name) CRUCIBLE_FUZZ_ENTRY_RUN_(name)
#define CRUCIBLE_FUZZ_ENTRY_SEEDS_(name) ::crucible::fuzz::boundary::entry::seeds_##name
#define CRUCIBLE_FUZZ_ENTRY_SEEDS(name) CRUCIBLE_FUZZ_ENTRY_SEEDS_(name)
