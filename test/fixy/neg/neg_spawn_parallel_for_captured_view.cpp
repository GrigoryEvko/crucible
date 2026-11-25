// The body of a parallel loop captures a View of the owner that became the
// region (rule 9 of foundation/Closure.h).  A shard would read an element of
// the run through the View while a second shard writes it.  The type of the
// View does not name its run, so the loop refuses each captured View of plain
// elements, and the body takes the run of an input from reads().

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    ::fixy::View<rig::Word const> const borrow_of_items{items.view()};
    rig::give_region_and_borrow(items, borrow_of_items, [](auto region, auto const& view) noexcept {
        return rig::loop_over(std::move(region), [view](auto shard, auto) noexcept {
            for (rig::Word& element : shard) {
                element = *view.get(0).expect("the run has an element");
            }
        });
    });
    return 0;
}
