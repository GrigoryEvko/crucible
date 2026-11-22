#pragma once

// An allocator that puts no constraint on its element, for the tests of the
// arena door of OwnedRegion.  The door itself must refuse a proof type, so
// the refusal does not rest on the allocator.  test_owned_region and the two
// fixtures neg_owned_region_unchecked_arena_* name it.
//
// The allocator gives no storage: each request gets the empty View.  The door
// never calls it for a type that it refuses, or for a count of zero.  For
// each other type and count the door ends the process, because the View does
// not hold the count of the request, and a death test of test_owned_region
// reads that end.
//
// The negative-compile fixtures of the region doors, of the shared read and
// of the parallel doors of fixy/concurrent/Spawn.h name it too.  They take a
// region from the arena door, and they never run, so no fixture calls the
// allocator.

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
