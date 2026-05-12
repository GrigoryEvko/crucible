// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-6 (#981): by_name<CallerRow>() is a pure projection
// over read-only registry state.  A caller declaring IO effects must
// fail the Subrow<CallerRow, Row<>> gate.

#include <crucible/Arena.h>
#include <crucible/RecipePool.h>
#include <crucible/RecipeRegistry.h>

namespace eff = ::crucible::effects;

int main() {
  crucible::Arena arena{};
  eff::Init init{};
  eff::Test test{};
  crucible::RecipePool pool{crucible::RecipePool::ArenaBorrow{arena}, init};
  crucible::RecipeRegistry reg{
      crucible::RecipeRegistry::PoolBorrow{pool}, test.alloc};
  auto wrong = reg.by_name<eff::Row<eff::Effect::IO>>(
      crucible::recipe_names::kF32Strict);
  return wrong.has_value() ? 0 : 1;
}
