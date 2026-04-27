// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A09 — PermissionedMpmcChannel::ProducerHandle exposes only
// try_push.  Calling try_pop must be a hard compile error.

#include <crucible/concurrent/PermissionedMpmcChannel.h>

namespace {

struct BadChan {};

void exercise_producer_try_pop() {
    crucible::concurrent::PermissionedMpmcChannel<int, 64, BadChan> ch;
    auto p_opt = ch.producer();
    if (!p_opt) return;
    auto producer = std::move(*p_opt);
    auto v = producer.try_pop();
    (void)v;
}

}  // namespace

int main() { exercise_producer_try_pop(); return 0; }
