// A TextView of view() named the characters in the storage of a GrowText.
// An append that grows the text moves the characters and frees the old
// storage, so the TextView dangled.  A GrowText gives no TextView out of a
// frame door, so the read of the characters across an append does not
// compile.  A text that no append changes is a HeapText, from freeze.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    ::foundation::core::GrowText text = ::foundation::core::mint_grow_text(4096);
    text.append(alloc, "crucible").expect("the text is below its maximum");
    auto const characters = text.view();
    for (int turn = 0; turn < 100; ++turn) {
        text.append(alloc, "0123456789").expect("the text is below its maximum");
    }
    return characters.size() == 8 ? 0 : 1;
}
