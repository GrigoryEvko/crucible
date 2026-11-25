// The body of a parallel loop captures a const reference to a View of the owner
// that became the region (rule 9 of foundation/Closure.h).  The reference reads
// the run as the View does, so the loop refuses it too.

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    ::fixy::View<rig::Word const> const borrow_of_items{items.view()};
    rig::give_region_and_borrow(items, borrow_of_items, [](auto region, auto const& view) noexcept {
        return rig::loop_over(std::move(region), [&view](auto shard, auto) noexcept {
            for (rig::Word& element : shard) {
                element = *view.get(0).expect("the run has an element");
            }
        });
    });
    return 0;
}
