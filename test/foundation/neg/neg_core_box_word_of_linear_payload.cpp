// A word of a Box is not linear, and no path consumes the object of a word.
// So a Box of a linear payload gives no word.  A Hole is linear, so a Box of
// a Hole is a Box of a linear payload.

#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <utility>

using Credit = ::foundation::core::Hole<8, 8>;

class Channel {
public:
    static ::foundation::core::BoxWord<Credit, Channel> seal(::foundation::core::Box<Credit>&& box) {
        return std::move(box).release_to_word(::foundation::core::box_word_key<Channel>{});
    }
};

int main() {
    constexpr auto alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::Box<Credit> box =
        ::foundation::core::mint_box<Credit>(alloc, ::foundation::core::mint_hole<8, 8>(alloc));
    return Channel::seal(std::move(box)).is_empty() ? 1 : 0;
}
