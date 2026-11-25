// The body of a parallel loop captures a Ref of an element of the owner that
// became the region (rule 9 of foundation/Closure.h).  A second shard would
// write the element while this shard reads it.

#include "../parallel_loop_rig.h"

#include <foundation/core/Ref.h>

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    ::fixy::View<rig::Word const> const view{items.view()};
    auto const last_element = view.get(rig::kCount - 1).expect("the run has the element");
    rig::give_region_and_borrow(items, last_element, [](auto region, auto const& last) noexcept {
        return rig::loop_over(std::move(region), [last](auto shard, auto) noexcept {
            for (rig::Word& element : shard) {
                element = *last;
            }
        });
    });
    return 0;
}
