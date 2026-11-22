// A loop over an owning Option would lend its payload and count no lend.  The
// body assigns the Option, which ends the HeapArray that the reference of the
// loop names, and a View of that array would then read freed storage.  An
// owning Option has no loop, so the loop does not compile.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Effect.h>

namespace fc = ::foundation::core;

int main() {
    fc::Option<fc::HeapArray<int>> held =
        fc::Option<fc::HeapArray<int>>::some(fc::mint_heap_array(::foundation::effects::testing::test().alloc, 64, 5));
    int sum = 0;
    for (fc::HeapArray<int> const& array : held) {
        fc::View<int const> const elements = array.view();
        held = fc::none;
        for (int const element : elements)
            sum += element;
    }
    return sum;
}
