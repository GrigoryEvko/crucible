// The body of a parallel loop reads two inputs, and reads() names one of them
// (rule 9 of foundation/Closure.h).  The second one enters through a capture,
// where no compare sees it, so the loop refuses the body.

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> const first = rig::make_items();
    ::fixy::HeapArray<rig::Word> const second = rig::make_items();
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    ::fixy::View<rig::Word const> const unnamed{second.view()};
    items = ::fixy::with_region<rig::Written>(std::move(items), [&first, unnamed](auto region) noexcept {
        return rig::loop_over(
            std::move(region),
            [unnamed](auto shard, auto, auto named) noexcept {
                for (rig::Word& element : shard) {
                    element = named.size() + unnamed.size();
                }
            },
            first);
    });
    return 0;
}
