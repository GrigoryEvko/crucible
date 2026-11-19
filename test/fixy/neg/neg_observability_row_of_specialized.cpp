// B002 reads the row that an Observability grade names.  The map was once
// the class template observability_row_of_.  This file tries to give a
// surface that observes IO the empty row, so that a binding that observes
// IO passes as one that observes nothing: it specializes that class
// template.  The map is a deduction, and no class template gives it, so
// the specialization does not compile.

#include <fixy/Collision.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::collision::detail::observability_row_of_<fixy::atom::observe::surface<foundation::effects::Effect::IO>> {
    using type = foundation::effects::Row<>;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
