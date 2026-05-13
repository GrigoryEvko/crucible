// ═══════════════════════════════════════════════════════════════════
// fuzz_serialize — adversarial-input fuzz harness for deserialize_region.
//
// Feeds arbitrary bytes through Serialize.h's bounded Reader.  The
// deserializer must:
//   - Return nullptr on any malformed input (no crash, no abort)
//   - Allocate at most CDAG_MAX_OBJECT_BYTES of arena space
//   - Trip ASan / UBSan only on a real bug
//
// Sanitizers (built into the default preset) catch:
//   - Out-of-bounds reads (Reader cursor mishandling)
//   - Integer overflow in size calculations
//   - Use-of-uninitialized in the parsed structure
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Arena.h>
#include <crucible/Effects.h>
#include <crucible/Serialize.h>

#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Cap the input at MAX_OBJECT_BYTES.  Above that, the loader
    // (Cipher::load) rejects upstream; the deserializer's contracts
    // assume bounded input.  This mirrors the real entry-path guard.
    constexpr size_t kMaxBytes = size_t{256} << 20;  // 256 MB
    if (size > kMaxBytes) size = kMaxBytes;

    crucible::Arena arena;
    crucible::fx::Test test{};
    auto* region = crucible::deserialize_region(
        test.alloc,
        std::span<const uint8_t>{data, size},
        arena);
    // Either valid (non-null) or invalid (nullptr); both are
    // acceptable outcomes — the harness tests the NO-CRASH property
    // on the entire byte space.
    (void)region;
    return 0;
}

#define CRUCIBLE_FUZZ_STANDALONE_MAIN
#include "runner_main.h"
