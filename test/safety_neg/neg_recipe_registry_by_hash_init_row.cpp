// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-6 (#981): by_hash<CallerRow>() is a pure projection.
// Init is a construction/startup row, not a pure lookup row, so the
// call must fail before the hash lookup body is considered.

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
  auto wrong = reg.by_hash<eff::Row<eff::Effect::Init>>(
      crucible::RecipeHash{0x1234});
  return wrong.has_value() ? 0 : 1;
}
