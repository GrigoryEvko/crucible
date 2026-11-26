// Negative-compile test.  This file must fail to compile.
//
// A body of with_compiled of one context captures the key of a second context.
// One key opens each context, because the brand names no context, so the key
// would open a second scope of the first context in the body.  The gate refuses
// a body that reaches any key of a context.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext first{test.alloc};
    auto first_key = crucible::mint_context_key(test);
    auto second_key = crucible::mint_context_key(test);
    first.with_compiled(first_key, [&second_key](crucible::CompiledScope& scope) noexcept {
        static_cast<void>(scope);
        static_cast<void>(second_key);
    });
    return 0;
}
