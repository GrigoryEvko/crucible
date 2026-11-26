// Negative compile test.  This file must fail to compile.
//
// The arrays of an op are a run-set slot of a frame (TraceEntry of
// crucible/MerkleDag.h).  Only a store of the frame of the op fills the slot,
// so the doors of the arrays take the frame and a holder of the op: a
// FrameRef or a FrameElement of the same frame.  An op on the stack is no
// holder, because no frame made it, so a run of the frame would join an
// object outside each arena.
//
// Expected diagnostic: no door of the scalars takes a plain op.

#include <crucible/MerkleDag.h>

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    ::foundation::core::with_arena_frame(arena, [](auto& frame) noexcept -> void {
        const auto alloc = ::foundation::effects::testing::test().alloc;
        crucible::TraceEntry plain_op{};
        crucible::TraceEntry::set_scalar_args(frame, plain_op, frame.array(alloc, 1, int64_t{0}));
    });
    return 0;
}
