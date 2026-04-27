// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A09 — PermissionedMpmcChannel::ConsumerHandle exposes only
// try_pop.  Calling try_push must be a hard compile error — the
// type system enforces role discipline that no existing MPMC
// library encodes.

#include <crucible/concurrent/PermissionedMpmcChannel.h>

namespace {

struct BadChan {};

void exercise_consumer_try_push() {
    crucible::concurrent::PermissionedMpmcChannel<int, 64, BadChan> ch;
    auto c_opt = ch.consumer();
    if (!c_opt) return;
    auto consumer = std::move(*c_opt);
    consumer.try_push(42);
}

}  // namespace

int main() { exercise_consumer_try_push(); return 0; }
