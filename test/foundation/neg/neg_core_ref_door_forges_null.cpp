// The rule specialization-home refuses an explicit specialization of the
// member template RefDoor::lend outside Ref.h, and this probe writes one in
// an opt-out region.  The specialization is a member of the door, so it
// reaches the key, but Ref grants the door no friend.  So the body of such a
// specialization cannot write the pointer of a Ref, and it cannot make a
// null Ref.  The member of the door is private, so the probe does not call
// it, and the compile reads the body of the specialization alone.

#include <foundation/Quarantine.h>
#include <foundation/core/Ref.h>

struct ForgeBrand {};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the probe specializes the Ref door to show that it cannot write the pointer")
template <>
constexpr foundation::core::Ref<int, ForgeBrand>
foundation::core::detail::RefDoor::lend<ForgeBrand, int>(int& object) noexcept {
    foundation::core::Ref<int, ForgeBrand> forged{object, RefKey{}};
    forged.object_ = nullptr;
    return forged;
}
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
