#pragma once

// An allocator that puts no constraint on its element, for the tests of the
// arena door of OwnedRegion.  The door itself must refuse a proof type, so
// the refusal does not rest on the allocator.  test_owned_region and the two
// fixtures neg_owned_region_unchecked_arena_* name it.
//
// The allocator gives no storage.  The door never calls it for a type that it
// refuses, and the tests call it for no other type.  The door asks for a
// member that gives a raw pointer, and no base type gives that shape, so the
// member stands in a PROBE region.

#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace test_owned_region {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: an allocator with no constraint gives the raw pointer of the arena door")
class UncheckedArena {
public:
    template <typename T>
    [[nodiscard]] T* alloc_array(::foundation::effects::Alloc, std::size_t) noexcept {
        return nullptr;
    }
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace test_owned_region
