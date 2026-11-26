// A frame stores a family Ref that to() gives into a link slot.  to() gives a
// Ref of the family from each plain reference, so the Ref proves no arena.
// A store takes only a FrameRef of its own frame, so the call does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

struct Base {
    using brand_type = TestFamily;

    explicit constexpr Base(fc::ArenaBuildKey<TestFamily, Base> /*key*/) noexcept {}
    Base(Base const&) = delete("a node lives in its arena");
    Base& operator=(Base const&) = delete("a node lives in its arena");
    ~Base() = default;

    fc::FrameLink<Base, TestFamily> next{};
};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::BumpArena<TestFamily> other =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::Ref<Base, TestFamily> const outside = other.make<Base>(alloc);
    fc::with_arena_frame(arena, [alloc, outside](auto& frame) noexcept {
        auto const head = frame.template make<Base>(alloc);
        frame.store(head, &Base::next, fc::to(*outside));
    });
    return 0;
}
