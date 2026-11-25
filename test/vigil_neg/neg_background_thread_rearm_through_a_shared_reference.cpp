// Negative-compile test.  This file must fail to compile.
//
// A rearm of the stop signal reads the owner of the pipeline thread, which a
// start writes with no order.  So a rearm of one thread beside a start of
// another one is a race.  The rearm is a door of the Stopped handle, which one
// thread has, and a shared reference has no such door.

#include <crucible/BackgroundThread.h>

namespace {

// The thread is a parameter, so the fixture builds no ring and no log for it.
[[maybe_unused]] void rearm(crucible::BackgroundThread const& background) { background.rearm_stop(); }

}  // namespace

int main() { return 0; }
