// The region of a parallel loop holds rows of the shape of PointwiseRows of
// fixy/concurrent/ParallelPointwise.h, in a class that does not state
// sendable{} (design note 6 of fixy/concurrent/Spawn.h).  The rows write
// through a strided view that copies, so two shards could write one element,
// and only a form that proves the opposite states the fact.

#include "../parallel_loop_rig.h"

#include <foundation/core/Choice.h>
#include <foundation/core/Strided.h>

namespace {

namespace rig = ::parallel_loop_rig;

class UnstatedRows final {
    using Rows = ::foundation::core::StridedView<rig::Word, 1>;

    ::fixy::Option<Rows> rows_{::fixy::none};

public:
    void hold(Rows rows) noexcept { rows_ = ::fixy::Option<Rows>::some(std::move(rows)); }
};

}  // namespace

int main() {
    ::fixy::HeapArray<UnstatedRows> rows{};
    rows = ::fixy::with_region<rig::Written>(std::move(rows), [](auto region) noexcept {
        return rig::loop_over(std::move(region), [](auto shard, auto) noexcept { static_cast<void>(shard); });
    });
    return 0;
}
