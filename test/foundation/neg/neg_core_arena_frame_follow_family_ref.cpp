// A frame follows the link slot of an object through a family Ref of the
// object.  A family Ref can name an object of any arena of the family, so it is
// no holder of the frame, and the call does not compile.

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
    fc::BumpArena<TestFamily> arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto const first = frame.template make<Node>(alloc);
        auto const second = frame.template make<Node>(alloc);
        frame.store(first, &Node::next, second);
        fc::Ref<Node, TestFamily> const family = first.weaken();
        auto const reached = frame.follow(family, &Node::next);
        static_cast<void>(reached);
    });
    return 0;
}
