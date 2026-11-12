// A file outside Ref.h writes an explicit specialization of mint_box.  The
// rules of the base refuse such a specialization, and this probe writes it in
// an opt-out region.  Box has no constructor from a pointer and grants
// mint_box no friend, so the body of the specialization cannot build a Box
// over storage that the allocation door did not give.

#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

using AllocationTag = foundation::effects::Alloc;

int stolen = 0;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a forged mint of a Box shows that Box has no constructor from a pointer")
template <>
foundation::core::Box<int> foundation::core::mint_box<int, AllocationTag>(AllocationTag /*allocation*/) noexcept {
    return foundation::core::Box<int>{&stolen};
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
