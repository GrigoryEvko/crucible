// A body of with_arena_frame gives a FrameRef of its frame.  A value that names
// the brand of a frame stays in the call of its door, so a later frame of the
// same call site cannot take it, and the call does not compile.

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
    auto kept = fc::with_arena_frame(
        arena, [alloc](auto& frame) noexcept { return frame.template make<std::uint32_t>(alloc, std::uint32_t{7}); });
    return static_cast<int>(*kept);
}
