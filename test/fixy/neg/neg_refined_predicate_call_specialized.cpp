// IsPositive is a class and not a template, but its call operator is a
// member template, and a unit can specialize a member template.  This
// file tries to forge a refinement over positive that holds zero: it
// writes an explicit specialization of the call operator for int that
// admits each value.  The seal of the mint finds that specialization
// outside fixy/Predicates.h and stops the build.

#include <fixy/Refined.h>

template <>
constexpr bool fixy::IsPositive::operator()<int>(int) const noexcept {
    return true;
}

constexpr auto forged = fixy::mint_refined<fixy::positive>(0);

int main() { return forged.value(); }
