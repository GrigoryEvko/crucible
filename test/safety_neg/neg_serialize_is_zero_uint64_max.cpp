// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for the zero constants of write_meta in Serialize.h.
//
// write_meta writes the tensor data pointer and the gradient-function
// hash as zero.  Each zero is a constexpr fixy::Refined<is_zero,
// uint64_t>, so a later edit that feeds the live field in does not
// compile.  A refined constant that holds UINT64_MAX must stop the
// constant evaluation at the predicate.
//
// Distinct mismatch class from neg_serialize_is_zero_nonzero_construct.cpp:
//   * Companion: the boundary edge, 1.
//   * This fixture: the wide miss, UINT64_MAX.  It catches a refinement
//     that no longer runs its predicate, so that a process-local
//     address could reach the bytes on disk.

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<::fixy::is_zero>(UINT64_MAX);
    (void)bad;
    return 0;
}
