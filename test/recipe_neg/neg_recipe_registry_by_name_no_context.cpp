// Negative-compile test. This file must fail to compile.
//
// A caller that holds the context of a test runner, whose row holds Alloc,
// IO and Block, calls RecipeRegistry::by_name and names no row.  A gate whose
// row has a default admits such a caller, because the default row is the
// pure projection row whatever the row of the caller is.  The gate takes its
// row from the context, and no form with no context exists.
//
// Expected diagnostic: no by_name takes the name alone.

#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Ctx.h>

namespace eff = ::foundation::effects;

namespace {

[[nodiscard]] bool reads_from_an_allocating_caller(::fixy::TestRunnerCtx const&, crucible::RecipeRegistry const& reg) {
    return reg.by_name(crucible::recipe_names::kF32Strict).is_ok();
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx test{eff::testing::test()};
    ::fixy::BumpArena arena{};
    crucible::RecipePool pool{::fixy::to(arena), eff::testing::init()};
    crucible::RecipeRegistry reg{::fixy::to(pool), eff::testing::test().alloc};
    return reads_from_an_allocating_caller(test, reg) ? 0 : 1;
}
