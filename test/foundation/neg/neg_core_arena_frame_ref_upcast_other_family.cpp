// A FrameRef of an object of one family converts to a FrameRef of a base that
// states another family as its brand_type.  A FrameRef of the base would give
// a Ref of the family of the frame to an object that the base says is of the
// other family, so the line does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};
struct[[= ::foundation::brand::arena_family_brand{}]] OtherFamily final {};

// A base that states the other family, and a class of the family of the
// arena that derives from it.
struct ForeignBase {
    using brand_type = OtherFamily;
    std::uint32_t depth = 0;
};

struct Mixed : ForeignBase {
    std::uint32_t extra = 0;
};

// The FrameRef of the base Target of the object of made, of the same frame.
template <class Target, class Family, class Fresh, class Made>
void take_base(fc::FrameRef<Made, Family, Fresh> const& made) noexcept {
    fc::FrameRef<Target, Family, Fresh> const base = made;
    static_cast<void>(base);
}

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto const mixed = frame.template make<Mixed>(alloc);
        take_base<ForeignBase>(mixed);
    });
    return 0;
}
