// as_ref of an owning Option would give a Ref that outlives each lend that
// the Option counts: an assignment of the Option then ends the object that
// the Ref names, with no check.  An owning Option has no as_ref.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

namespace fc = ::foundation::core;

int main() {
    fc::Option<fc::HeapArray<int>> held =
        fc::Option<fc::HeapArray<int>>::some(fc::mint_heap_array(::foundation::effects::testing::test().alloc, 64, 5));
    auto const borrowed = fc::as_ref(held);
    held = fc::none;
    return borrowed.is_some() ? 1 : 0;
}
