// The main translation unit of one boundary fuzz binary.
//
// CMake compiles this file once per harness with CRUCIBLE_FUZZ_HARNESS_NAME
// set, and links it with the object of that harness, built from
// harness_entry.cpp.  A new boundary is a new header under harnesses/ and one
// line in fuzz/CMakeLists.txt.

#include "entry.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

CRUCIBLE_FUZZ_DECLARE_HARNESS_OF(CRUCIBLE_FUZZ_HARNESS_NAME)

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);
std::vector<std::vector<std::uint8_t>> crucible_fuzz_seeds();

int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    CRUCIBLE_FUZZ_ENTRY_RUN(CRUCIBLE_FUZZ_HARNESS_NAME)
    (size == 0 ? std::span<const std::uint8_t>{} : std::span<const std::uint8_t>{data, size});
    return 0;
}

std::vector<std::vector<std::uint8_t>> crucible_fuzz_seeds() {
    return CRUCIBLE_FUZZ_ENTRY_SEEDS(CRUCIBLE_FUZZ_HARNESS_NAME)();
}

#define CRUCIBLE_FUZZ_STANDALONE_MAIN
#include "runner_main.h"
