// The row hash folds the canonical identity of each lattice, and a cache
// key reads the row hash.  This file tries to give a forged lattice the
// identity of an honest one, so that the two share one cache slot: it
// specializes lattice_canonical_id as a class template.  The identity is a
// function at namespace scope that is not a template, and a type states a
// second identity only in its own definition, so the specialization does
// not compile.

#include <foundation/Quarantine.h>
#include <foundation/diag/RowHash.h>

#include <cstdint>

namespace forged_identity {
struct Forged {};
struct Honest {};
}  // namespace forged_identity

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct foundation::diag::lattice_canonical_id<forged_identity::Forged> {
    static constexpr std::uint64_t value = ::foundation::reflect::stable_type_id(^^forged_identity::Honest);
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
