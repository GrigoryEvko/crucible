// The body of a parallel loop captures a strided view of the owner that became
// the region (rule 9 of foundation/Closure.h).  A strided view reads the run as
// a View does.

#include "../parallel_loop_rig.h"

#include <foundation/core/Strided.h>

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    ::fixy::FixedArray<std::size_t, 1> const sizes{rig::kCount};
    ::fixy::FixedArray<std::size_t, 1> const strides{1};
    auto const strided_items =
        ::fixy::mint_strided_view(::fixy::View<rig::Word const>{items.view()}, sizes.view(), strides.view(), 0)
            .expect("the view fits the run");
    rig::give_region_and_borrow(items, strided_items, [](auto region, auto const& strided) noexcept {
        return rig::loop_over(std::move(region), [strided](auto shard, auto) noexcept {
            for (rig::Word& element : shard) {
                element = strided.count();
            }
        });
    });
    return 0;
}
