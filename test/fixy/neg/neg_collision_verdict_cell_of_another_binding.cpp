// The gate reads the verdict of a binding from its sealed cell.  Only the
// build of the cell makes one, and the build judges the binding that its key
// names.  This file gives the cell of borrow with coroutine, which L002
// refuses, the cell of borrow alone.  The gate reads the cell with the key of
// its own binding, so the read refuses the cell of another binding, and the
// forgery admits nothing.

#include <fixy/Reject.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives the verdict cell of a binding the cell of another binding")
template <>
inline constexpr fixy::collision::verdict_cell
    fixy::collision::verdict_cell_v<int, fixy::atom::borrow, fixy::atom::coroutine> =
        fixy::collision::build_verdict_cell(^^fixy::collision::verdict_key<int, fixy::atom::borrow>);
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::ValidComposition<int, fixy::atom::borrow, fixy::atom::coroutine>);

int main() { return 0; }
