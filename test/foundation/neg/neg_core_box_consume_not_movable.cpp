// consume() moves the object out of its storage.  An object with no move
// constructor cannot leave the storage, so consume() is refused for it.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

struct Fixed {
    int value = 0;
    explicit Fixed(int initial) noexcept : value{initial} {}
    Fixed(Fixed&&) = delete;
    Fixed(Fixed const&) = delete;
};

int main() {
    auto held = ::foundation::core::mint_box<Fixed>(::foundation::effects::Alloc{}, 3);
    return static_cast<decltype(held)&&>(held).consume().value;
}
