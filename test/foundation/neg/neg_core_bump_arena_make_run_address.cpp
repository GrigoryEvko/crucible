// Negative-compile test. This file must fail to compile.
//
// Violation: make builds an object that holds a RunAddress in a BumpArena.
// A RunAddress gives its pointer back (foreign_pointer), and a mint makes one
// from a run that a borrow names, so an object of an arena keeps no
// RunAddress.  It keeps the key of the address (ForeignAddressKey), which
// gives no pointer.

#include <foundation/core/BumpArena.h>
#include <foundation/core/DataAddress.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct HoldsAddress {
    ::foundation::core::Option<::foundation::core::RunAddress> address{::foundation::core::none};
};
}  // namespace

int main() {
    ::foundation::core::BumpArena<Family> arena = ::foundation::core::mint_bump_arena<Family>(
        ::foundation::core::Quantity<::foundation::core::unit::Byte, std::size_t>{4096});
    auto const held = arena.make<HoldsAddress>(::foundation::effects::testing::test().alloc, HoldsAddress{});
    return held->address.is_some() ? 1 : 0;
}
