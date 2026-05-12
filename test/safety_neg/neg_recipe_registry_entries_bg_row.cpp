// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipeReg-6 (#981): entries<CallerRow>() only exposes a
// read-only span over already-populated registry entries.  A Bg row
// is not pure and must not be accepted for this projection.

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
  auto wrong = reg.entries<eff::Row<eff::Effect::Bg>>();
  return wrong.value().empty() ? 0 : 1;
}
