// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-6 (#981): by_name_spec<CallerRow>() is the two-axis
// RecipeSpec projection.  It still only reads registry state, so a
// blocking caller row must be rejected at substitution.

#include <crucible/Arena.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>

namespace eff = ::crucible::effects;

int main() {
  crucible::Arena arena{};
  auto init = eff::testing::init();
  auto test = eff::testing::test();
  crucible::RecipePool pool{crucible::RecipePool::ArenaBorrow{arena}, init};
  crucible::RecipeRegistry reg{
      crucible::RecipeRegistry::PoolBorrow{pool}, test.alloc};
  auto wrong = reg.by_name_spec<eff::Row<eff::Effect::Block>>(
      crucible::recipe_names::kF32Strict);
  return wrong.has_value() ? 0 : 1;
}
