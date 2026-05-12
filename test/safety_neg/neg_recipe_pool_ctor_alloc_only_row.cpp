// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipePool-5 (#974): RecipePool construction does allocate
// table storage, but Alloc alone is not the startup phase.  CallerRow
// must include Init, not merely the value-level allocation capability.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Init>, Row<Alloc>>.

#include <crucible/RecipePool.h>
#include <crucible/effects/EffectRow.h>

#include <type_traits>

namespace eff = ::crucible::effects;

int main() {
  crucible::Arena arena{};
  eff::Init init{};
  crucible::RecipePool pool{
      crucible::RecipePool::ArenaBorrow{arena},
      init,
      32u,
      std::type_identity<eff::Row<eff::Effect::Alloc>>{}};
  return pool.capacity() == 0;
}
