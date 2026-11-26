// Negative-compile test.  This file must fail to compile.
//
// A body of with_compiled captures the key of its context.  With the key it
// could deactivate the context while the scope lends the engine and the pool,
// or open a second scope.  The gate refuses a body that reaches a key of a
// context.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    ctx.with_compiled(context_key, [&context_key](crucible::CompiledScope& scope) noexcept {
        static_cast<void>(scope);
        static_cast<void>(context_key);
    });
    return 0;
}
