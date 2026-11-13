// IsPositive is a class and not a template, but its call operator is a
// member template, and a unit can specialize a member template.  This
// file tries to forge a refinement over positive that holds zero: it
// writes an explicit specialization of the call operator for int that
// admits each value.  The rule specialization-home refuses that
// specialization, so the probe puts it in an opt-out region.  The seal of
// the mint finds the specialization outside fixy/Predicates.h and stops the
// build too.

#include <fixy/Refined.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the probe specializes a predicate call to show that the mint refuses it")
template <>
constexpr bool fixy::IsPositive::operator()<int>(int) const noexcept {
    return true;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

constexpr auto forged = fixy::mint_refined<fixy::positive>(0);

int main() { return forged.value(); }
