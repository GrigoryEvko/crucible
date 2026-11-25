// The body of a parallel loop captures a closure that holds a View of the owner
// that became the region (rule 9 of foundation/Closure.h).  A captured closure
// is a class, and its captures are its members, so the View refuses the body.

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    ::fixy::View<rig::Word const> const view{items.view()};
    auto const read_first = [view]() noexcept { return *view.get(0).expect("the run has an element"); };
    rig::give_region_and_borrow(items, read_first, [](auto region, auto const& first_of_run) noexcept {
        return rig::loop_over(std::move(region), [first_of_run](auto shard, auto) noexcept {
            for (rig::Word& element : shard) {
                element = first_of_run();
            }
        });
    });
    return 0;
}
