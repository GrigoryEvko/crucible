// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-G78 production-call-site fence — distinct from the wrapper-
// level RecipeSpec fences (neg_recipe_spec_axis_swap,
// neg_recipe_spec_admits_axis_swap, etc.) which exercise the
// constructor / method signatures.  This fixture pins the
// REGISTRY-API CALL-SITE bug class: a consumer that wants to
// admission-gate by RECIPE FAMILY (not tolerance tier) reaches for
// `by_name_pinned<>` (FOUND-G04, regime-1 single-axis Tolerance pin)
// and substitutes a `RecipeFamily` enumerator for the template
// parameter.  The compiler must reject — `by_name_pinned` is
// declared `template <safety::Tolerance T>` so a `RecipeFamily`
// argument is a substitution failure.
//
// THE BUG CLASS this catches:  a refactor wants to enforce that
// only Kahan-family recipes flow into the optimizer's moment-
// buffer accumulator (Adam β₁/β₂ moment EMAs are notoriously
// stability-sensitive — one Pairwise-family recipe slipped into
// that path corrupts the moment buffer with O(log n) ε growth
// where Kahan would deliver O(1)).  The author reaches for
// `by_name_pinned<RecipeFamily::Kahan>` thinking the same template
// machinery generalizes — it does not.  The single-axis pinned
// API is Tolerance-only by design (regime-1 static admission for
// the chain lattice).  Two-axis admission is regime-4 RUNTIME
// data carried in `RecipeSpec<T>`; the call site for that uses
// `by_name_spec(name)` then `.admits(req_tier, req_family)`.
//
// Without this compile-time fence the author would either:
//   (a) silently get a RELAXED-pin (RecipeFamily::Kahan = 2 in the
//       enum's underlying value, and `by_name_pinned<Tolerance{2}>`
//       happens to be Tolerance::ULP_FP8 in the chain — a wildly
//       wrong axis interpreted as a wildly wrong tier), or
//   (b) get a confused runtime error chain at admission time.
//
// Either way: cross-vendor numerics CI catches it 12 hours later.
// With this fence: caught at the registry-lookup boundary, naming
// the wrong template kind in the diagnostic.
//
// [GCC-WRAPPER-TEXT] — by_name_pinned<> non-type template parameter
// kind enforcement.

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <crucible/safety/RecipeSpec.h>

using namespace crucible;

int main() {
    Arena arena{};
    effects::Test test_ctx{};
    RecipePool     pool{arena, test_ctx.alloc};
    RecipeRegistry reg{pool,  test_ctx.alloc};

    // Should FAIL: by_name_pinned is `template <safety::Tolerance T>`;
    // a RecipeFamily enumerator is the wrong template kind.  The
    // bug class — wanting two-axis admission via the single-axis
    // pinned API — is captured at the substitution boundary.  Use
    // `reg.by_name_spec(name)` + `.admits(req_tier, req_family)`
    // for two-axis admission instead.
    auto wrong = reg.by_name_pinned<safety::RecipeFamily::Kahan>(
        recipe_names::kF32Strict);
    (void) wrong;
    return 0;
}
