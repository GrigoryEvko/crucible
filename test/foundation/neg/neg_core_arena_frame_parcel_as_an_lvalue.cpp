// A caller opens a frame on a parcel that it names as an lvalue.  The caller
// then keeps a name of the parcel, which could end its arena while the frame is
// open, so the door takes the parcel only by move, and the call does not
// compile.

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
    fc::Box<fc::BumpArena<TestFamily>> boxed = fc::mint_box<fc::BumpArena<TestFamily>>(
        alloc, fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    auto parcel = fc::mint_arena_parcel(std::move(boxed), [alloc](auto& frame) noexcept {
        auto items = frame.array(alloc, std::size_t{3}, std::uint32_t{7});
        return frame.seal(items);
    });
    fc::with_arena_frame(parcel, [](auto& /*frame*/, auto& /*items*/) noexcept {});
    return 0;
}
