// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-G78-AUDIT — cross-API result-type fence.  Companion to
// neg_recipe_registry_pinned_with_family_template_arg (FOUND-G78
// baseline) which fences the wrong-template-kind misuse.  This
// fixture pins the symmetric mistake: the consumer correctly
// reaches for `by_name_spec` (regime-4 two-axis runtime spec) but
// then types the result slot as if `by_name_pinned` (regime-1
// single-axis static admission) was called.  The two return types
// are STRUCTURALLY DISTINCT — `RecipeSpec<T>` carries a 2-byte
// runtime grade pair while `NumericalTier<T_static, T>` carries
// the tier as a non-type template parameter (zero-byte EBO-collapsed
// grade).  The compiler must reject the std::expected conversion
// because the inner E template arguments do not match.
//
// THE BUG CLASS this catches:  a Forge Phase E.RecipeSelect
// integration that already wired by_name_spec for the two-axis
// admission gate gets refactored — someone copies the result-type
// declaration from a sibling call site that uses by_name_pinned,
// and now `auto&& spec = reg.by_name_spec(...)` becomes
// `expected<NumericalTier<BITEXACT, const Recipe*>, RecipeError> spec
//   = reg.by_name_spec(...)`.  Without this fence the silent
// convertible-from-anything trap could mask the type drift; with
// it, the compiler names the mismatched expected<> templates.
//
// Why this is G78-distinct (not G04 or G75/76 territory):
//   - G04's wrapper-level fences cover NumericalTier compile-time
//     admission.
//   - G75/76's wrapper-level fences cover RecipeSpec axis-swap at
//     constructor / admits().
//   - G78 baseline covered template-arg-kind mismatch on the
//     pinned API.
//   - This G78-AUDIT fence is the SYMMETRIC misuse: correct
//     two-axis API call + wrong single-axis result type.  Catches
//     the "I copy-pasted a sibling call site's return type"
//     refactor footgun.
//
// [GCC-WRAPPER-TEXT] — std::expected<RecipeSpec<T>, _> →
// std::expected<NumericalTier<T_static, T>, _> conversion
// rejection.

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

    // Should FAIL: by_name_spec returns
    //   std::expected<safety::RecipeSpec<const NumericalRecipe*>, RecipeError>
    // but the consumer typed the result slot as
    //   std::expected<safety::NumericalTier<BITEXACT, const NumericalRecipe*>, RecipeError>
    // The two are structurally distinct — RecipeSpec is regime-4
    // (per-instance 2-byte grade), NumericalTier is regime-1 (zero-
    // byte EBO-collapsed static grade).  No implicit conversion
    // exists between them; the compiler rejects the
    // std::expected<T, E> template argument deduction with a
    // structured diagnostic naming both result types.
    std::expected<
        safety::NumericalTier<safety::Tolerance::BITEXACT,
                              const NumericalRecipe*>,
        RecipeError> wrong_slot = reg.by_name_spec(recipe_names::kF32Strict);
    (void) wrong_slot;
    return 0;
}
