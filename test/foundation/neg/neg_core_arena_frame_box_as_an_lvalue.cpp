// A caller opens a frame on the arena of a Box that it names as an lvalue.  The
// caller then keeps a name of the Box, which could end the arena while the frame
// is open, so the door takes the Box only by move, and the call does not
// compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <cstddef>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::Box<fc::BumpArena<TestFamily>> boxed = fc::mint_box<fc::BumpArena<TestFamily>>(
        alloc, fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(boxed, [](auto& /*frame*/) noexcept {});
    return 0;
}
