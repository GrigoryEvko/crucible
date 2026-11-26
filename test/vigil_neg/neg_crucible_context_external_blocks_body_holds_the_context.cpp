// Negative-compile test.  This file must fail to compile.
//
// The body of with_external_blocks gets the compiled scope as an argument.  A body
// that holds a context by a reference could store a View of the scope in it, and
// the View would outlive the scope.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto given = ctx.with_compiled(context_key, [alloc = test.alloc](crucible::CompiledScope& scope) noexcept {
        crucible::CrucibleContext other{alloc};
        auto kept = scope.with_external_blocks(crucible::ExternalBlocks{}, [&other](crucible::CompiledScope&) noexcept {
            static_cast<void>(other.is_compiled());
        });
        static_cast<void>(kept);
    });
    static_cast<void>(given);
    return 0;
}
