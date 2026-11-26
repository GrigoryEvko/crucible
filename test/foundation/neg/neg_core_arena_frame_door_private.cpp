// A unit calls the door class of foundation/core/ArenaFrame.h to open a frame
// of a brand that it picks on an arena.  The members of the door are private
// to the four doors of the header, so the call does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>

#include <cstddef>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};
struct PickedBrand final {};

}  // namespace

int main() {
    fc::BumpArena<TestFamily> arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    auto frame = fc::ArenaFrameDoor::open<TestFamily, PickedBrand>(arena);
    static_cast<void>(frame);
    return 0;
}
