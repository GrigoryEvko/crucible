// The body of a parallel loop captures a const reference to a borrow of the
// region from before the region moves into the loop (rule 9 of
// foundation/Closure.h).  The reference reads the region as the borrow does.

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    items = ::fixy::with_region<rig::Written>(std::move(items), [](auto region) noexcept {
        auto const borrow = ::fixy::mint_borrowed(region);
        return rig::loop_over(std::move(region), [&borrow](auto shard, auto) noexcept {
            for (rig::Word& element : shard) {
                element = *borrow.get(0).expect("the run has an element");
            }
        });
    });
    return 0;
}
