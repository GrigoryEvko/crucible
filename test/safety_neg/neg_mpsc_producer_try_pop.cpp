// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A05 — PermissionedMpscChannel::ProducerHandle exposes only
// try_push.  Calling try_pop on it must be a hard compile error
// because:
//   (a) MpscRing's try_pop has a single-consumer contract and any
//       producer also calling it would race with the real consumer.
//   (b) Multiple ProducerHandles can exist concurrently — if one of
//       them could pop, we'd have many concurrent poppers, which
//       MpscRing structurally cannot support (no per-cell sequence
//       on the consumer side).
//
// Expected diagnostic substring (GCC-WRAPPER-TEXT — overload-set
// rejection because try_pop is not a member of ProducerHandle):
//   "no member named .try_pop."

#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/permissions/Permission.h>

namespace {

struct BadChan {};

void exercise_producer_try_pop() {
    crucible::concurrent::PermissionedMpscChannel<int, 64, BadChan> ch;
    auto p_opt = ch.producer();
    if (!p_opt) return;
    auto producer = std::move(*p_opt);

    // PRODUCER attempting POP — try_pop is structurally absent
    // from ProducerHandle's API surface.
    auto v = producer.try_pop();
    (void)v;
}

}  // namespace

int main() { exercise_producer_try_pop(); return 0; }
