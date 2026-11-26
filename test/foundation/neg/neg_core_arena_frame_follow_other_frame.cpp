// A frame on one arena follows the link slot of a holder that a frame on
// another arena of the same family made.  The holder is no FrameRef of the
// frame that follows, so the call does not compile: the FrameRef that it
// would give could name an object of the other arena.

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
        auto const first = outer.template make<Node>(alloc);
        auto const second = outer.template make<Node>(alloc);
        outer.store(first, &Node::next, second);
        fc::with_arena_frame(inner_arena, [alloc, &first](auto& inner) noexcept {
            auto const holder = inner.template make<Node>(alloc);
            auto const reached = inner.follow(first, &Node::next);
            static_cast<void>(reached);
            static_cast<void>(holder);
        });
    });
    return 0;
}
