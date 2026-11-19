// An effect row maps to its mask, and a gate that compares masks reads
// the mask of the row.  This file tries to give Row<IO> the empty mask, so
// that a row that does IO compares as pure: it specializes the trait
// spelling row_descriptor_v.  The mask is a function at namespace scope
// that is not a template, so the specialization does not compile.

#include <foundation/Quarantine.h>
#include <foundation/effects/Row.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
inline constexpr foundation::effects::EffectRowLattice::element_type
    foundation::effects::row_descriptor_v<foundation::effects::Row<foundation::effects::Effect::IO>> = 0;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(foundation::effects::row_descriptor_v<foundation::effects::Row<foundation::effects::Effect::IO>> == 0);

int main() { return 0; }
