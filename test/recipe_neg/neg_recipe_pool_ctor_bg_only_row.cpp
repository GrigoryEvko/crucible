// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Background work can intern recipes later through intern(Alloc, ...),
// but the construction of the pool is initialization work.  A CallerRow
// that holds only Bg must not satisfy the constructor gate.
//
// Expected diagnostic: the Subrow<Row<Init>, Row<Bg>> constraint of the
// constructor is not satisfied.

#include <crucible/RecipePool.h>
#include <fixy/Borrowed.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace eff = ::foundation::effects;

int main() {
    crucible::Arena arena{};
    auto init = eff::testing::init();
    crucible::RecipePool pool{::fixy::mint_borrowed_ref(arena), init, 32u,
                              std::type_identity<eff::Row<eff::Effect::Bg>>{}};
    return pool.capacity() == 0;
}
