// The body of the frame door with_view of a GrowText keeps the TextView of
// the characters in a variable that it captures by reference.  The TextView
// outlives the frame, so a clear of the text leaves it with characters past
// the count (test_core_grow_text_poison.cpp).  The store check of the door
// (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the body.

#include <foundation/core/Grow.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Effect.h>

int main() {
    auto text = ::foundation::core::mint_grow_text(64);
    text.append(::foundation::effects::testing::test().alloc, ::foundation::core::TextView{"kept"})
        .expect("the text has room");
    ::foundation::core::TextView kept{};
    text.with_view([&kept](::foundation::core::TextView characters) noexcept { kept = characters; });
    return static_cast<int>(kept.size());
}
