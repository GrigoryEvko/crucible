// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-G78-AUDIT-2 — symmetric companion to
// neg_recipe_registry_spec_assigned_to_pinned_expected.  Where that
// fence catches the "two-axis API call → one-axis result slot"
// drift, this fence catches the OTHER direction: a consumer that
// calls the regime-1 single-axis `by_name_pinned<>` overload but
// types the std::expected slot as the regime-4 two-axis
// `by_name_spec` return (i.e., wants to use admits(req_tier,
// req_family) on the result without realizing the pinned API
// returned a single-axis NumericalTier wrapper that has neither
// .recipe_family() nor .admits()).
//
// Together with the sibling fence, the two-axis call-site bug
// class is closed in BOTH directions:
//   spec → pinned slot (sibling)  — copy-pasted result type from
//                                    a single-axis call site
//   pinned → spec slot (this)     — copy-pasted result type from
//                                    a two-axis call site
//
// THE BUG CLASS this catches:  a Forge Phase E.RecipeSelect
// integration that wired by_name_pinned<BITEXACT> for compile-
// time tolerance admission gets refactored — someone widens the
// admission to two-axis (tolerance + family) by changing only
// the result-type slot from NumericalTier to RecipeSpec without
// also updating the call to by_name_spec.  The pinned API still
// runs, returning a NumericalTier; the slot is RecipeSpec; the
// std::expected<T, E> conversion is rejected with a structured
// diagnostic naming both result types.
//
// Diagnostic phrase from GCC 16: "conversion from
// 'expected<NumericalTier<...>, _>' to non-scalar type
// 'expected<RecipeSpec<...>, _>' requested".  The matching
// regex covers both "conversion from" and "non-scalar type"
// — same surface as the sibling fence.
//
// [GCC-WRAPPER-TEXT] — std::expected<NumericalTier<T_static, T>, _>
// → std::expected<RecipeSpec<T>, _> conversion rejection.

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/RecipeSpec.h>

#include <expected>

using namespace crucible;

int main() {
    Arena arena{};
    effects::Test test_ctx{};
    RecipePool     pool{arena, test_ctx.alloc};
    RecipeRegistry reg{pool,  test_ctx.alloc};

    // Should FAIL: by_name_pinned<BITEXACT> returns
    //   std::expected<safety::NumericalTier<BITEXACT, const NumericalRecipe*>, RecipeError>
    // but the consumer typed the result slot as
    //   std::expected<safety::RecipeSpec<const NumericalRecipe*>, RecipeError>
    // The two are structurally distinct — NumericalTier is regime-1
    // (zero-byte EBO-collapsed static grade), RecipeSpec is regime-4
    // (per-instance 2-byte runtime grade).  No implicit conversion
    // between them; the std::expected<T, E> deduction rejects.
    std::expected<safety::RecipeSpec<const NumericalRecipe*>, RecipeError>
        wrong_slot = reg.by_name_pinned<safety::Tolerance::BITEXACT>(
            recipe_names::kF32Strict);
    (void) wrong_slot;
    return 0;
}
