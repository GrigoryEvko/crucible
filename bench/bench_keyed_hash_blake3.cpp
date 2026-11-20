// The door of bench_keyed_hash to the pinned official BLAKE3 (utils/pins.txt).
// The library blake3_reference of bench/CMakeLists.txt holds its C and its
// assembly, and the door calls its C interface.  The library compiles
// blake3_dispatch.c with BLAKE3_TESTING, the switch of the official tests
// that makes the set of CPU features of the dispatch a variable that a caller
// can set, and with BLAKE3_ATOMICS=0, so that variable is a plain int.

#include "bench_keyed_hash_references.h"

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the pinned official BLAKE3 is a C library, the reference of the keyed hash")

#include "blake3.h"

// The CPU features of the official dispatch, which BLAKE3_TESTING makes
// visible (blake3_dispatch.c).
extern "C" int g_cpu_features;

namespace bench_keyed_hash_references {

namespace {

// The bits of enum cpu_feature of blake3_dispatch.c at the pinned commit.
constexpr int kSse2 = 1 << 0;
constexpr int kSsse3 = 1 << 1;
constexpr int kSse41 = 1 << 2;
constexpr int kAvx = 1 << 3;
constexpr int kAvx2 = 1 << 4;
// No feature read yet: the next hash reads the features of the host.
constexpr int kUndefined = 1 << 30;

// The features of an arm.
[[nodiscard]] constexpr int features_of(Blake3Arm arm) noexcept {
    if (arm == Blake3Arm::avx2) return kSse2 | kSsse3 | kSse41 | kAvx | kAvx2;
    if (arm == Blake3Arm::sse41) return kSse2 | kSsse3 | kSse41;
    if (arm == Blake3Arm::sse2) return kSse2;
    return kUndefined;
}

}  // namespace

void blake3_keyed(std::byte const* key, std::byte const* first, std::size_t count, std::byte* out) noexcept {
    blake3_hasher hasher{};
    blake3_hasher_init_keyed(&hasher, static_cast<unsigned char const*>(static_cast<void const*>(key)));
    blake3_hasher_update(&hasher, first, count);
    blake3_hasher_finalize(&hasher, static_cast<unsigned char*>(static_cast<void*>(out)), BLAKE3_OUT_LEN);
}

void blake3_pick_arm(Blake3Arm arm) noexcept { g_cpu_features = features_of(arm); }

}  // namespace bench_keyed_hash_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
