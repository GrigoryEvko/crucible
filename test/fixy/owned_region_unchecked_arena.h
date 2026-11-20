#pragma once

// An allocator that puts no constraint on its element, for the tests of the
// arena door of OwnedRegion.  The door itself must refuse a proof type, so
// the refusal does not rest on the allocator.  test_owned_region and the two
// fixtures neg_owned_region_unchecked_arena_* name it.
//
// The allocator gives no storage: each request gets the empty View.  The door
// never calls it for a type that it refuses.  For each other type the door
// ends the process, because the View does not hold the count of the request,
// and a death test of test_owned_region reads that end.

#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace test_owned_region {

class UncheckedArena {
public:
    template <typename T>
    [[nodiscard]] ::foundation::core::View<T> alloc_array(::foundation::effects::Alloc, std::size_t) noexcept {
        return ::foundation::core::View<T>{};
    }
};

}  // namespace test_owned_region
