// A frame on one arena stores a node of a frame on another arena of the same
// family into a link slot of its own node.  The two frames have two scope
// brands, so the value is no FrameRef of the frame that stores, and the call
// does not compile.

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
    std::uint32_t value = 0;
    fc::FrameLink<Node, TestFamily> next{};
};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> outer_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::BumpArena<TestFamily> inner_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(outer_arena, [alloc, &inner_arena](auto& outer) noexcept {
        auto const kept = outer.template make<Node>(alloc);
        fc::with_arena_frame(inner_arena, [alloc, kept](auto& inner) noexcept {
            auto const holder = inner.template make<Node>(alloc);
            inner.store(holder, &Node::next, kept);
        });
    });
    return 0;
}
