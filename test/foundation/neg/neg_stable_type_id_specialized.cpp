// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The row hash folds the stable id of each lattice, and a cache key reads
// the row hash.  A unit that could specialize the id would give a forged
// lattice the identity of an honest one, and the two would share a cache
// slot.  The id is a function that is not a template, so the
// specialization does not compile.

#include <foundation/diag/RowHash.h>

#include <cstdint>

namespace forged_lattice {
struct Forged {};
struct Honest {};
}  // namespace forged_lattice

template <>
inline constexpr std::uint64_t foundation::reflect::stable_type_id<forged_lattice::Forged> = 1;

[[maybe_unused]] inline constexpr std::uint64_t read_id =
    foundation::diag::lattice_canonical_id_v<forged_lattice::Forged>;

int main() { return 0; }
