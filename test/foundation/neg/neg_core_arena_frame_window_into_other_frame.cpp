// A frame on one arena stores a window of a run of a frame on another arena of
// the same family into a run slot of its own node.  A window keeps the frame
// of its run, so the call does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

struct Node {
    fc::FrameRunSlot<std::uint32_t const, TestFamily> items{};
};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> outer_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::BumpArena<TestFamily> inner_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(outer_arena, [alloc, &inner_arena](auto& outer) noexcept {
        auto items = outer.array(alloc, std::size_t{4}, std::uint32_t{7});
        auto part =
            items.window(fc::RunStart{fc::ElementCount{1}}, fc::ElementCount{2}).expect("the run holds four items");
        fc::with_arena_frame(inner_arena, [alloc, &part](auto& inner) noexcept {
            auto const holder = inner.template make<Node>(alloc);
            inner.store(holder, &Node::items, part);
        });
    });
    return 0;
}
