// A loop over an owning Option would lend its payload and count no lend.  The
// body ends the HeapArray that the reference of the loop names, and a read
// through that reference then reads the ended array.  An owning Option has no
// loop, so the loop does not compile.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Effect.h>

namespace fc = ::foundation::core;

int main() {
    fc::Option<fc::HeapArray<int>> held =
        fc::Option<fc::HeapArray<int>>::some(fc::mint_heap_array(::foundation::effects::testing::test().alloc, 64, 5));
    int size = -1;
    for (fc::HeapArray<int> const& array : held) {
        held = fc::none;
        size = static_cast<int>(array.size());
    }
    return size;
}
