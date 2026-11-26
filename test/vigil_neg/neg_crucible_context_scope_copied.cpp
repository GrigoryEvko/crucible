// Negative-compile test.  This file must fail to compile.
//
// A body of with_compiled copies its scope into a local.  A copy could leave the
// body by a value that the gate of the result does not read as a borrow.  The
// scope has no copy and no move.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto given = ctx.with_compiled(context_key, [](crucible::CompiledScope& scope) noexcept {
        crucible::CompiledScope copied{scope};
        static_cast<void>(copied);
    });
    static_cast<void>(given);
    return 0;
}
