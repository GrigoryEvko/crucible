// The region of a parallel loop holds parts of the shape of ScanPart of
// fixy/concurrent/ParallelTree.h, in a class that does not state sendable{}
// (design note 6 of fixy/concurrent/Spawn.h).  A part is the one writer of its
// run, and a reader of the loop can name the same run through an input, so only
// a form that proves the opposite states the fact.

#include "../parallel_loop_rig.h"

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

namespace {

namespace rig = ::parallel_loop_rig;

template <class Brand>
class UnstatedPart final {
    ::fixy::Option<::fixy::ViewMut<rig::Word, Brand>> run_{::fixy::none};

public:
    void hold(::fixy::ViewMut<rig::Word, Brand>&& run) noexcept {
        run_ = ::fixy::Option<::fixy::ViewMut<rig::Word, Brand>>::some(std::move(run));
    }
};

}  // namespace

int main() {
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    items = ::fixy::with_mut(std::move(items), [](auto lent) noexcept {
        using Part = UnstatedPart<typename decltype(lent)::brand_type>;
        ::fixy::HeapArray<Part> parts{};
        parts = ::fixy::with_region<rig::Written>(std::move(parts), [](auto region) noexcept {
            return rig::loop_over(std::move(region), [](auto shard, auto) noexcept { static_cast<void>(shard); });
        });
    });
    return 0;
}
