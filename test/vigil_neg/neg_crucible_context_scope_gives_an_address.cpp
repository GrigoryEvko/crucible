// Negative-compile test.  This file must fail to compile.
//
// A body of with_compiled gives the address of an output of the matched op
// back.  The address would outlive the scope, and a deactivation after it would
// end the pool that the address names.  An address leaves the replay only
// through the keyed doors read_output_address and read_input_address, so a
// result of a body holds no borrow.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto kept = ctx.with_compiled(context_key, [](crucible::CompiledScope& scope) noexcept {
        return scope.output_ptr(crucible::OutputPosition{0});
    });
    static_cast<void>(kept);
    return 0;
}
