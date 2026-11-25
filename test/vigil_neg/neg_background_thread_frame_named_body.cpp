// Negative-compile test.  This file must fail to compile.
//
// The frame of with_background_thread owns its body and calls it one time.  A
// body that the caller names keeps a second name that can call it again, so the
// door refuses it.

#include <crucible/BackgroundThread.h>

#include <foundation/effects/Effect.h>

namespace {

// The sides are parameters, so the fixture builds no ring and no log.
[[maybe_unused]] void open_with_a_name(crucible::BackgroundThread::TraceRingSide& ring_side,
                                       crucible::BackgroundThread::MetaLogSide& log_side) {
    auto body = [](crucible::BackgroundThread const&, auto stopped) noexcept {
        static_cast<void>(stopped.pending_op_count());
    };
    crucible::with_background_thread(::foundation::effects::testing::test().alloc, ring_side, log_side, {}, body);
}

}  // namespace

int main() { return 0; }
