// A file outside Array.h writes an explicit specialization of
// mint_heap_array.  The rules of the base refuse such a specialization, and
// this probe writes it in an opt-out region.  HeapArray has no constructor
// from a pointer and grants mint_heap_array no friend, so the body of the
// specialization cannot build an owner of storage that the allocation door
// did not give, which the destructor would free.

#include <foundation/Quarantine.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

using AllocationTag = foundation::effects::Alloc;

int stolen = 0;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged mint shows that HeapArray has no constructor from a pointer")
template <>
foundation::core::HeapArray<int>
foundation::core::mint_heap_array<int, alignof(int), AllocationTag>(AllocationTag /*allocation*/, std::size_t count,
                                                                    int const& /*fill*/) noexcept {
    return foundation::core::HeapArray<int>{&stolen, count};
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
