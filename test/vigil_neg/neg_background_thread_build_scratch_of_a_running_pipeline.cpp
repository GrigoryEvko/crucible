// Negative-compile test.  This file must fail to compile.
//
// The build stage writes its scratch arena with no order.  So a read of the
// bytes of the arena is a door of a Stopped handle, and a Running handle has no
// such door.

#include <crucible/BackgroundThread.h>

namespace {

struct Scope final {};

// The handle is a parameter, so the fixture builds no ring and no log.
[[maybe_unused]] size_t read_bytes(crucible::BackgroundThread::Running<Scope> const& running) {
    return running.build_scratch_retained_bytes().raw();
}

}  // namespace

int main() { return 0; }
