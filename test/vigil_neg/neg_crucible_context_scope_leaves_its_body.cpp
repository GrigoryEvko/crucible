// Negative-compile test.  This file must fail to compile.
//
// A body of with_compiled gives a Ref of its scope back.  The scope would
// outlive its body, and a door of it would read the engine and the pool after a
// deactivation ended them.  The result of a body holds no borrow.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    crucible::CrucibleContext ctx{test.alloc};
    auto context_key = crucible::mint_context_key(test);
    auto kept = ctx.with_compiled(
        context_key, [](crucible::CompiledScope& scope) noexcept { return ::fixy::erase_brand(::fixy::to(scope)); });
    static_cast<void>(kept);
    return 0;
}
