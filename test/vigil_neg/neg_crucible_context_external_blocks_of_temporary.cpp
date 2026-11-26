// Negative-compile test.  This file must fail to compile.
//
// A scope of external slots lends the pool of its context to the body.  A
// temporary context releases its pool at the end of the statement, so it lends
// no compiled scope.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    auto context_key = crucible::mint_context_key(test);
    crucible::CrucibleContext{test.alloc}.with_compiled(context_key, [](crucible::CompiledScope& scope) noexcept {
        auto given = scope.with_external_blocks(crucible::ExternalBlocks{}, [](crucible::CompiledScope&) noexcept {});
        static_cast<void>(given);
    });
    return 0;
}
