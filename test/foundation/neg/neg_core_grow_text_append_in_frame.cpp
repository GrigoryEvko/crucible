// The body of the frame door with_view appends to its GrowText.  The append
// can move the characters that the body borrows.  The door carries the
// annotation borrow_match, and the rule borrow-match refuses a call of a
// member of the text that is not const in the body.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::GrowText text = ::foundation::core::mint_grow_text(64);
    text.append(alloc, "abc").expect("the text is below its maximum");
    std::size_t const size = text.with_view([&](::foundation::core::TextView characters) noexcept {
        static_cast<void>(text.append(alloc, characters));
        return characters.size();
    });
    return static_cast<int>(size);
}
