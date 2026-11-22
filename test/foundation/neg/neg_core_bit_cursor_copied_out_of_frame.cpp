// A body of the frame door with_keys copies a cursor of the walk.  A copy
// could leave the frame, through a capture, and outlive the set, and its keys
// would then read words that a different object reuses.  A cursor does not
// copy and does not move, so it lives only in the frame.

#include <foundation/core/BitSet.h>

int main() {
    ::foundation::core::BitSet<8> set{};
    set.set(::foundation::core::index<3>());
    return set.with_keys([](auto const& keys) noexcept {
        auto cursor = keys.begin();
        auto copy = cursor;
        return static_cast<int>((*copy).raw());
    });
}
