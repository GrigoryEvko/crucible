// A gate that compares effect rows reads the mask of each row.  The mask
// was once the variable template row_descriptor_v.  This file tries to give
// Row<IO> the empty mask, so that a row that does IO compares as pure: it
// specializes that variable.  The mask comes from a function, and no
// variable gives it, so the specialization does not compile.

#include <foundation/Quarantine.h>
#include <foundation/effects/Row.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
inline constexpr foundation::effects::EffectRowLattice::element_type
    foundation::effects::row_descriptor_v<foundation::effects::Row<foundation::effects::Effect::IO>> = 0;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
