// A body of with_arena_frame gives the seal of its runs, outside the mint of a
// parcel.  The seal names the brand of the frame, so it stays in the call of
// its door, and the call does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    auto sealed = fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto items = frame.array(alloc, std::size_t{3}, std::uint32_t{7});
        return frame.seal(items);
    });
    static_cast<void>(sealed);
    return 0;
}
