// A region of Boxes takes no compared input (reads() of
// fixy/concurrent/Spawn.h).  The runner compares the bytes of the Boxes with the
// bytes of the input.  A shard writes the object of a Box, which lies in neither
// run, so an input that is a View of such an object would pass the compare.

#include "../parallel_loop_rig.h"

#include <foundation/core/Ref.h>

namespace {

namespace rig = ::parallel_loop_rig;

[[nodiscard]] ::fixy::HeapArray<::fixy::Box<rig::Word>> make_boxes() noexcept {
    auto const alloc = rig::eff::testing::test().alloc;
    return ::fixy::mint_heap_array_with<::fixy::Box<rig::Word>>(
        alloc, rig::kCount, [alloc](std::size_t) noexcept { return ::fixy::mint_box<rig::Word>(alloc, rig::Word{1}); });
}

}  // namespace

int main() {
    ::fixy::HeapArray<rig::Word> const input = rig::make_items();
    ::fixy::HeapArray<::fixy::Box<rig::Word>> boxes = make_boxes();
    boxes = ::fixy::with_region<rig::Written>(std::move(boxes), [&input](auto region) noexcept {
        return rig::loop_over(
            std::move(region),
            [](auto shard, auto, auto run) noexcept {
                static_cast<void>(shard);
                static_cast<void>(run);
            },
            input);
    });
    return 0;
}
