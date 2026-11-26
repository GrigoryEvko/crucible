// A unit builds an ArenaParcel from a Box and the runs of another arena, with
// no frame and no mint.  The constructor of a parcel is private to the door of
// the mint, so the parcel does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Record.h>
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
    fc::BumpArena<TestFamily> other =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::Record<fc::ArenaRun<std::uint32_t, TestFamily>> runs{other.array(alloc, std::size_t{3}, std::uint32_t{7})};
    fc::Box<fc::BumpArena<TestFamily>> boxed = fc::mint_box<fc::BumpArena<TestFamily>>(
        alloc, fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::ArenaParcel<TestFamily, std::uint32_t> parcel{std::move(boxed), runs};
    static_cast<void>(parcel);
    return 0;
}
