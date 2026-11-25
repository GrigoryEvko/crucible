// The combine of reduce_by_tree_in_parallel holds a View of the partials, which
// the shards write (rule 9 of foundation/Closure.h).  The combine is a capture
// of the body of the shards, so the parallel loop of the form refuses it.

#include "../parallel_loop_rig.h"

#include <fixy/concurrent/ParallelTree.h>

namespace {

namespace rig = ::parallel_loop_rig;

struct AddsTheFirstPartial {
    ::fixy::View<rig::Word const> partials;

    [[nodiscard]] rig::Word operator()(rig::Word const& left, rig::Word const& right) const noexcept {
        return left + right + *partials.get(0).expect("the run has an element");
    }
};

}  // namespace

int main() {
    rig::BgCtx const ctx{rig::eff::testing::bg()};
    ::fixy::HeapArray<rig::Word> const items = rig::make_items();
    ::fixy::View<rig::Word const> const elements{items.view()};
    ::fixy::HeapArray<rig::Word> slots = ::fixy::mint_heap_array(rig::eff::testing::test().alloc, 1, rig::Word{0});
    slots = ::fixy::with_region<rig::Written>(std::move(slots), [&ctx, elements](auto region) noexcept {
        AddsTheFirstPartial const combine{::fixy::mint_borrowed(region)};
        auto parts =
            ::fixy::concurrent::reduce_by_tree_in_parallel<::fixy::spawn::BodyCoupling::Independent, rig::kShards>(
                ctx, elements, std::move(region), combine);
        static_cast<void>(parts.value);
        return std::move(parts.partials);
    });
    return 0;
}
