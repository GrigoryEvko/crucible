// A file outside Ref.h can write an explicit specialization of the member
// template RefDoor::lend.  Ref grants RefDoor no friend, so the body of such
// a specialization cannot write the pointer of a Ref, and it cannot make a
// null Ref.

#include <foundation/core/Ref.h>

struct ForgeBrand {};

template <>
constexpr foundation::core::Ref<int, ForgeBrand>
foundation::core::detail::RefDoor::lend<ForgeBrand, int>(int& object) noexcept {
    foundation::core::Ref<int, ForgeBrand> forged{object, RefKey{}};
    forged.object_ = nullptr;
    return forged;
}

int main() {
    int target = 1;
    return *foundation::core::detail::RefDoor::lend<ForgeBrand>(target);
}
