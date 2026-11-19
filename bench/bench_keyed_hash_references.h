#pragma once

// The door of bench_keyed_hash to the pinned official BLAKE3 (utils/pins.txt,
// CC0 1.0 or Apache 2.0 at the choice of the user).  The door hashes count
// bytes from first under the 32 bytes of a key in its keyed mode, and writes
// the 32 bytes of the output.  The bench compiles the door only when
// bench/CMakeLists.txt sets CRUCIBLE_BENCH_BLAKE3, which it does on x86-64
// when the tools directory holds the pinned source (python3 utils/bin/setup
// install blake3).

#include <foundation/Quarantine.h>

#include <cstddef>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the door of the pinned reference of the keyed hash of the base")

namespace bench_keyed_hash_references {

#if defined(CRUCIBLE_BENCH_BLAKE3)
// The keyed hash of the official C and assembly, with the arm that its
// dispatch picks on the host.
void blake3_keyed(std::byte const* key, std::byte const* first, std::size_t count, std::byte* out) noexcept;
#endif

}  // namespace bench_keyed_hash_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
