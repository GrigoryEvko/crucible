// A function that takes the name and the shape of to() in its own namespace
// calls the door of a Ref to make a Ref of a brand that its caller names.
// The friend is foundation::core::to, so the member of the door is private
// here.

#include <foundation/core/Ref.h>

namespace lookalike {

template <class Brand, class T>
[[nodiscard]] constexpr foundation::core::Ref<T, Brand> to(T& object) noexcept {
    return foundation::core::detail::RefDoor::lend<Brand>(object);
}

}  // namespace lookalike

struct ArenaBrand {};

int main() {
    int target = 1;
    return *lookalike::to<ArenaBrand>(target);
}
