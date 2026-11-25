// Negative-compile test.  This file must fail to compile.
//
// A BackgroundThread takes the consumer sides of its trace ring and of its
// metadata log in its constructor, and each start reads those two sides.  So
// start takes no side, and a start after stop cannot name a ring or a log
// other than the two of the constructor.

#include <crucible/BackgroundThread.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace {

struct Scope final {};

// The handle and the two sides are parameters, so the fixture builds no ring
// and no log.
[[maybe_unused]] void start_with(crucible::BackgroundThread::Stopped<Scope> stopped,
                                 crucible::BackgroundThread::TraceRingSide& ring_side,
                                 crucible::BackgroundThread::MetaLogSide& log_side) {
    static_cast<void>(
        std::move(stopped).start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, ring_side, log_side));
}

}  // namespace

int main() { return 0; }
