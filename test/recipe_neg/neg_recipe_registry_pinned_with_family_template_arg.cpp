// Negative-compile test. This file must fail to compile.
//
// by_name_pinned is declared template <fixy::Tolerance T>.  A consumer
// that wants to admit by recipe family must not reach for it with a
// RecipeFamily enumerator.  The pinned lookup gates the tolerance axis
// only.  Admission on the two axes uses by_name_spec(name) and then
// fixy::admits(spec, tier, family).
//
// Without this refusal, RecipeFamily::Kahan has the value 2, and a
// conversion to Tolerance would read it as ULP_FP8, which is a different
// axis and a wrong tier.
//
// Expected diagnostic: the RecipeFamily enumerator cannot convert to the
// Tolerance template parameter of by_name_pinned.

#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <fixy/Bands.h>
#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

using namespace crucible;

int main() {
    auto arena = ::fixy::mint_bump_arena<crucible::RecipeArena>(::fixy::Quantity<::fixy::unit::Byte, size_t>{1 << 20});
    auto test_ctx = ::foundation::effects::testing::test();
    auto init_ctx = ::foundation::effects::testing::init();
    RecipePool pool{::fixy::to(arena), init_ctx};
    RecipeRegistry reg{::fixy::to(pool), test_ctx.alloc};

    auto wrong = reg.by_name_pinned<::fixy::RecipeFamily::Kahan>(::foundation::effects::testing::foreground(),
                                                                 recipe_names::kF32Strict);
    (void)wrong;
    return 0;
}
