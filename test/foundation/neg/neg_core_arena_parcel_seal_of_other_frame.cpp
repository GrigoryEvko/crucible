// The body of mint_arena_parcel gives the seal of an outer frame on another
// arena.  The parcel would then hold the Box of one arena and the runs of
// another, so the mint takes only a seal of its own frame, and the call does
// not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> outer_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(outer_arena, [alloc](auto& outer) noexcept {
        auto items = outer.array(alloc, std::size_t{3}, std::uint32_t{7});
        fc::Box<fc::BumpArena<TestFamily>> boxed = fc::mint_box<fc::BumpArena<TestFamily>>(
            alloc, fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
        auto parcel = fc::mint_arena_parcel(std::move(boxed),
                                            [&outer, &items](auto& /*frame*/) noexcept { return outer.seal(items); });
        static_cast<void>(parcel);
    });
    return 0;
}
