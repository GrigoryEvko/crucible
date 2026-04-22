// ═══════════════════════════════════════════════════════════════════
// fuzz_recipe_bytes — feed arbitrary 16-byte inputs through
// compute_recipe_hash via std::bit_cast.
//
// NumericalRecipe is layout-locked at 16 bytes (sizeof asserted in
// the header) with seven 1-byte enum fields + flags + 8-byte hash.
// Many enum values are reserved (e.g., ScalarType::Undefined = -1)
// or out-of-range (a u8 reading 7 for a 4-variant enum).  The hash
// function must NOT crash on ANY 16-byte input — sign-extension via
// the bit_cast<uint8_t> guard, fmix64's saturation tolerance, and
// the absence of any pre() contract on enum-validity all need to
// hold across the byte space.
//
// AFL++ QEMU-mode driving:
//   afl-fuzz -Q -i fuzz/boundary/corpus -o /tmp/afl_recipe -- ./build/fuzz/fuzz_recipe_bytes @@
// Sanitizers abort on any UB; AFL++ logs crashing input to
// /tmp/afl_recipe/default/crashes/.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/NumericalRecipe.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // bit_cast requires exact size; pad with zeros if input is short,
    // truncate if long.  Both paths must be crash-free.
    std::array<uint8_t, sizeof(crucible::NumericalRecipe)> buf{};
    const size_t copy = (size < buf.size()) ? size : buf.size();
    for (size_t i = 0; i < copy; ++i) buf[i] = data[i];

    const auto recipe = std::bit_cast<crucible::NumericalRecipe>(buf);
    const auto h = crucible::compute_recipe_hash(recipe);

    // Doesn't matter what we do with the hash — the property is
    // just "no crash".  Force the optimizer to keep the call.
    asm volatile("" :: "r"(h.raw()));
    return 0;
}

#define CRUCIBLE_FUZZ_STANDALONE_MAIN
#include "runner_main.h"
