// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-6 (#981): by_name_pinned<T, CallerRow>() performs
// lookup plus tier admission over immutable registry state.  Alloc is
// not needed for the projection and must fail the pure-row gate.

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
  auto wrong = reg.by_name_pinned<
      crucible::safety::Tolerance::BITEXACT,
      eff::Row<eff::Effect::Alloc>>(crucible::recipe_names::kF32Strict);
  return wrong.has_value() ? 0 : 1;
}
