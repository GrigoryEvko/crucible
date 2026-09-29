// The translation unit of one harness.
//
// CMake compiles this file once per harness, with CRUCIBLE_FUZZ_HARNESS_NAME
// set to the harness name and CRUCIBLE_FUZZ_HARNESS_HEADER set to its header
// under harnesses/.  The header defines run_<name> and seeds_<name> in
// crucible::fuzz::boundary.  This file exports them under the names that
// entry.h declares.

#include "entry.h"

#include CRUCIBLE_FUZZ_HARNESS_HEADER

CRUCIBLE_FUZZ_DECLARE_HARNESS_OF(CRUCIBLE_FUZZ_HARNESS_NAME)

#define CRUCIBLE_FUZZ_DEFINE_ENTRY_(name)                                                    \
    void crucible::fuzz::boundary::entry::run_##name(std::span<const std::uint8_t> bytes) {  \
        ::crucible::fuzz::boundary::run_##name(bytes);                                       \
    }                                                                                        \
    std::vector<std::vector<std::uint8_t>> crucible::fuzz::boundary::entry::seeds_##name() { \
        return ::crucible::fuzz::boundary::seeds_##name();                                   \
    }
#define CRUCIBLE_FUZZ_DEFINE_ENTRY(name) CRUCIBLE_FUZZ_DEFINE_ENTRY_(name)

CRUCIBLE_FUZZ_DEFINE_ENTRY(CRUCIBLE_FUZZ_HARNESS_NAME)
