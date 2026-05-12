// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RecipePool-5 (#974): RecipePool construction is startup-only
// initialization work.  The constructor's CallerRow must contain
// RecipePool::init_required_row = Row<Init>; a pure row cannot build
// the interning table.
//
// [GCC-WRAPPER-TEXT] - requires-clause constraint failure on
// Subrow<Row<Init>, Row<>>.

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
      std::type_identity<eff::Row<>>{}};
  return pool.capacity() == 0;
}
