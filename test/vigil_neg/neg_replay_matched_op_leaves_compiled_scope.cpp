// Negative-compile test.  This file must fail to compile.
//
// A body of with_compiled gives the proof of a matched op back.  The proof
// names an entry of the active region, and a deactivation after the scope
// would end that region.  The proof is a borrow, so the result of a body
// holds none.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto kept = ctx.with_compiled(context_key, [](crucible::CompiledScope& scope) noexcept {
        return scope.matched_op(scope.advance(crucible::SchemaHash{1}, crucible::ShapeHash{2}))
            .expect("the scope matched the op");
    });
    static_cast<void>(kept);
    return 0;
}
