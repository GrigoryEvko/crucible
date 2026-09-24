// A refinement over a closure predicate takes no row hash.
//
// The predicate's type is a template argument of the refinement's
// lattice, and the lattice's printed name keys the hash.  A closure
// prints a name that depends on its signature and on its position in
// the translation unit, so the hash refuses it at compile time rather
// than hash it differently in each unit.

#include <fixy/Refined.h>
#include <foundation/diag/RowHash.h>

namespace closure_predicate {
inline constexpr auto odd = [](int value) { return value % 2 != 0; };
}  // namespace closure_predicate

int main() {
    return static_cast<int>(
        ::foundation::diag::row_hash_contribution_v<::fixy::Refined<closure_predicate::odd, int>> & 1U);
}
