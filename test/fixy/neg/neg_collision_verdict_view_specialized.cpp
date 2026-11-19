// The gate reads the verdict of a binding from its sealed cell, with the key
// of the binding, and never from the view binding_verdict_v.  This file
// specializes the view of borrow with coroutine, which L002 refuses, so that
// the view gives the verdict of borrow alone.  The view then says that the
// pack is valid, and the binding stays refused at tier 5 with L002.

#include <fixy/Fn.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges the view of a verdict, and the gate still refuses the binding")
template <>
inline constexpr fixy::collision::binding_verdict
    fixy::collision::binding_verdict_v<int, fixy::atom::borrow, fixy::atom::coroutine> =
        fixy::collision::binding_verdict_v<int, fixy::atom::borrow>;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    static_assert(fixy::collision::binding_verdict_v<int, fixy::atom::borrow, fixy::atom::coroutine>.is_valid());
    [[maybe_unused]] fixy::fn<int, fixy::atom::borrow, fixy::atom::coroutine> refused{};
    return 0;
}
