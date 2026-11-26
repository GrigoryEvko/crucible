// The region of a parallel loop holds Views that write one run (design note 6
// of fixy/concurrent/Spawn.h).  Each shard owns its own Views, and two Views
// write the same elements, so two shards race with no capture.  The loop takes
// a region whose elements are SendableBeside.

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<::fixy::View<rig::Word>> views{};
    views = ::fixy::with_region<rig::Written>(std::move(views), [](auto region) noexcept {
        return rig::loop_over(std::move(region), [](auto shard, auto) noexcept {
            for (::fixy::View<rig::Word> const& view : shard) {
                for (rig::Word& element : view) {
                    element = rig::Word{0};
                }
            }
        });
    });
    return 0;
}
