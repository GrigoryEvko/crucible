// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A09 — PermissionedMpmcChannel::ConsumerHandle is move-only.
// Copying it would duplicate a SharedPermissionGuard refcount share
// → consumer_pool_'s outstanding count would be wrong (under-counted)
// → with_drained_access could spuriously succeed while a copy is
// still alive, racing the body against the copied handle.

#include <crucible/concurrent/PermissionedMpmcChannel.h>

namespace {

struct BadChan {};

void exercise_consumer_copy() {
    crucible::concurrent::PermissionedMpmcChannel<int, 64, BadChan> ch;
    auto c_opt = ch.consumer();
    if (!c_opt) return;
    auto consumer = std::move(*c_opt);

    // Copy attempt — deleted with reason.
    auto consumer2 = consumer;
    (void)consumer2;
}

}  // namespace

int main() { exercise_consumer_copy(); return 0; }
