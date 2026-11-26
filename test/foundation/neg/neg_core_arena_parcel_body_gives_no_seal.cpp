// The body of mint_arena_parcel gives a run of its frame and no seal.  Only a
// seal of the frame of the mint makes a parcel, so the call does not compile.

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
        return frame.array(alloc, std::size_t{3}, std::uint32_t{7}).size();
    });
    static_cast<void>(parcel);
    return 0;
}
