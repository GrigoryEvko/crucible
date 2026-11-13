// The rule specialization-home refuses an explicit specialization of the
// member template RefDoor::lend outside Ref.h, and this probe writes one in
// an opt-out region.  Ref grants RefDoor no friend, so the body of such a
// specialization cannot write the pointer of a Ref, and it cannot make a
// null Ref.

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

int main() {
    int target = 1;
    return *foundation::core::detail::RefDoor::lend<ForgeBrand>(target);
}
