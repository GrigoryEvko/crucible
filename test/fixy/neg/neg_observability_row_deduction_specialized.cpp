// The row that an Observability grade names comes from the return type of
// a deduction.  This file tries to give a surface that observes IO the
// empty row: it writes an explicit specialization of the deduction that
// returns the empty row.  An explicit specialization keeps the return type
// of the template, so no template declaration matches it.

#include <fixy/Collision.h>

template <>
foundation::effects::Row<> fixy::collision::detail::observability_parts_::row_of<foundation::effects::Effect::IO>(
    const fixy::atom::observe::surface<foundation::effects::Effect::IO>*) noexcept;

int main() { return 0; }
