// A run of Views is no input of a parallel loop (reads() of
// fixy/concurrent/Spawn.h).  The runner compares the bytes of the Views with
// the region of the shards.  A View of the region, which is an element of the
// run, would pass that compare, and a body would read the region through it.

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> const items = rig::make_items();
    ::fixy::FixedArray<::fixy::View<rig::Word const>, 1> const views{items.view()};
    [[maybe_unused]] auto inputs = ::fixy::spawn::reads(views);
    return 0;
}
