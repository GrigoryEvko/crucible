// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipePool-5 (#974): Bg work may intern recipes later through
// intern(effects::Alloc, ...), but constructing the pool itself is
// initialization-phase work.  A Bg-only CallerRow must not satisfy
// the constructor gate.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Init>, Row<Bg>>.

#include <crucible/RecipePool.h>
#include <crucible/effects/EffectRow.h>

#include <type_traits>

namespace eff = ::crucible::effects;

int main() {
  crucible::Arena arena{};
  auto init = eff::testing::init();
  crucible::RecipePool pool{
      crucible::RecipePool::ArenaBorrow{arena},
      init,
      32u,
      std::type_identity<eff::Row<eff::Effect::Bg>>{}};
  return pool.capacity() == 0;
}
