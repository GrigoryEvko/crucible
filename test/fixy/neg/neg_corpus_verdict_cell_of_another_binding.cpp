// The corpus gate reads the premises of a binding from its sealed verdict
// cell, with the key of the binding.  This file gives the verdict cell of a
// ghost binding with IO, which the entry ghost_runtime_observable refuses, the
// cell of the ghost binding alone.  The corpus reads the cell with the key of
// its own binding, so the read refuses the cell of another binding, and the
// forgery admits nothing.

#include <fixy/Reject.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives the corpus the verdict cell of another binding")
template <>
inline constexpr fixy::collision::verdict_cell
    fixy::collision::verdict_cell_v<int, fixy::atom::ghost, fixy::atom::with_io> =
        fixy::collision::build_verdict_cell(^^fixy::collision::verdict_key<int, fixy::atom::ghost>);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::NotInCorpus<int, fixy::atom::ghost, fixy::atom::with_io>);

int main() { return 0; }
