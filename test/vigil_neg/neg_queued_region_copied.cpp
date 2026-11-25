// Negative-compile test.  This file must fail to compile.
//
// One push gives one QueuedRegion, and the memory of the region holds it.  A
// copy would let two memories name one push, so a QueuedRegion only moves.

#include <crucible/BackgroundThread.h>

namespace {

using Queued = crucible::BackgroundThread::QueuedRegion;

// The push is a parameter, so the fixture builds no queue.
[[maybe_unused]] Queued take_a_second_one(Queued const& queued) { return Queued{queued}; }

}  // namespace

int main() { return 0; }
