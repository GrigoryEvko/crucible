// Negative-compile test.  This file must fail to compile.
//
// The frame of with_background_thread ends the thread when its body returns.  A
// body that keeps its handle in an object of static storage would keep a handle
// of a thread that ended.  The type of the handle holds the scope brand of the
// frame, and no object of static storage holds such a brand.

#include <crucible/BackgroundThread.h>

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace {

template <class Handle>
::fixy::Option<Handle>& kept_handle() {
    static ::fixy::Option<Handle> kept{};
    return kept;
}

// The sides are parameters, so the fixture builds no ring and no log.
[[maybe_unused]] void keep_the_handle(crucible::BackgroundThread::TraceRingSide& ring_side,
                                      crucible::BackgroundThread::MetaLogSide& log_side) {
    crucible::with_background_thread(::foundation::effects::testing::test().alloc, ring_side, log_side, {},
                                     [](crucible::BackgroundThread const&, auto stopped) noexcept {
                                         static_cast<void>(kept_handle<decltype(stopped)>().is_some());
                                     });
}

}  // namespace

int main() { return 0; }
