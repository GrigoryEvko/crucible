#pragma once

// The doors of bench_reflect to the pinned rapidhash of Nicolas De Carli
// (utils/pins.txt, the MIT license).  Each door hashes count records of one
// fixed size from first, and gives the xor of the hashes, so one call covers
// a batch and the cost of the call is a small part of the row.  The size of
// a record is a constant of the door, as the size of an image is a constant
// of the hash of the family.  The bench compiles the doors only when
// bench/CMakeLists.txt sets CRUCIBLE_BENCH_RAPIDHASH, which it does when the
// tools directory holds the pinned source (python3 utils/bin/setup install
// rapidhash).

#include <foundation/Quarantine.h>

#include <cstddef>
#include <cstdint>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the doors of the pinned reference of the hash of the Protocol family")

namespace bench_reflect_references {

#if defined(CRUCIBLE_BENCH_RAPIDHASH)
// rapidhashNano, the variant for a key of at most 48 bytes, of records of 16
// bytes.
std::uint64_t rapidhash_nano_16(std::byte const* first, std::size_t count) noexcept;

// rapidhash of records of 88 bytes.
std::uint64_t rapidhash_88(std::byte const* first, std::size_t count) noexcept;
#endif

}  // namespace bench_reflect_references

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
