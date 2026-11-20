#pragma once

// The door of bench_keyed_hash to the pinned official BLAKE3 (utils/pins.txt,
// CC0 1.0 or Apache 2.0 at the choice of the user).  The door hashes count
// bytes from first under the 32 bytes of a key in its keyed mode, and writes
// the 32 bytes of the output.  It also picks the arm of the official
// dispatch, so a row of the official C can run the arm of the x86-64 level
// of the library of the base.  The bench compiles the door only when
// bench/CMakeLists.txt sets CRUCIBLE_BENCH_BLAKE3, which it does on x86-64
// when the tools directory holds the pinned source (python3 utils/bin/setup
// install blake3).

#include <foundation/Quarantine.h>

#include <cstddef>
#include <cstdint>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the door of the pinned reference of the keyed hash of the base")

namespace bench_keyed_hash_references {

// The arms of the official dispatch: the arm that it picks on the host, and
// the arm of each x86-64 level below it.  The level v1 holds SSE2, v2 holds
// SSE4.1 and v3 holds AVX2.
enum class Blake3Arm : std::uint8_t {
    host,
    avx2,
    sse41,
    sse2
};

#if defined(CRUCIBLE_BENCH_BLAKE3)
// The keyed hash of the official C and assembly, with the arm that the last
// pick gives, or the arm of the host with no pick.
void blake3_keyed(std::byte const* key, std::byte const* first, std::size_t count, std::byte* out) noexcept;

// Makes each hash of the official C that follows run the arm.
void blake3_pick_arm(Blake3Arm arm) noexcept;
#endif

}  // namespace bench_keyed_hash_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
