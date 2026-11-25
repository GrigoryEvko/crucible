// copy_at of a GrowList gives a value that outlives each frame of the list.  An
// element that holds a borrow would take that borrow out of the frame, so the
// call binds to the deleted overload.

#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

struct NamesARun {
    ::foundation::core::View<int const> run;
};

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    auto list = ::foundation::core::mint_grow_list<NamesARun>(8);
    list.append(alloc, NamesARun{}).expect("the list is below its maximum");
    return list.copy_at(0).is_some() ? 0 : 1;
}
