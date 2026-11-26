// A frame on one arena stores a run of a frame on another arena of the same
// family into a set of runs of its own object.  The run is no FrameRun of the
// frame that stores, so the call does not compile.

#include <foundation/core/ArenaFrame.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/RunSet.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>

namespace {

namespace fc = ::foundation::core;

struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

struct Op {
    fc::FrameRunSetSlot<std::uint16_t, TestFamily, fc::RunOf<std::uint32_t const, 0>> arrays{};
};

}  // namespace

int main() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> outer_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::BumpArena<TestFamily> inner_arena =
        fc::mint_bump_arena<TestFamily>(fc::ArenaBlockSize{fc::Quantity<fc::unit::Byte, std::size_t>{4096}});
    fc::with_arena_frame(outer_arena, [alloc, &inner_arena](auto& outer) noexcept {
        auto metas = outer.array(alloc, std::size_t{3}, std::uint32_t{7});
        fc::with_arena_frame(inner_arena, [alloc, &metas](auto& inner) noexcept {
            auto const op = inner.template make<Op>(alloc);
            inner.template store_runs<0, 0>(op, &Op::arrays, metas);
        });
    });
    return 0;
}
