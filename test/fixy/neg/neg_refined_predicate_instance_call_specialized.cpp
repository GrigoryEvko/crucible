// A unit can specialize the call operator of one instance of a predicate
// class template and keep the class itself.  This file tries to forge a
// refinement over in_range<1, 9> that holds zero: it writes an explicit
// specialization of the call operator of InRange<1, 9> for int that
// admits each value.  The seal of the mint finds that specialization
// outside fixy/Predicates.h and stops the build.

#include <fixy/Refined.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges the call operator of InRange<1, 9> for int")
template <>
template <>
constexpr bool fixy::InRange<1, 9>::operator()<int>(int) const noexcept {
    return true;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

constexpr auto forged = fixy::mint_refined<fixy::in_range<1, 9>>(0);

int main() { return forged.value(); }
