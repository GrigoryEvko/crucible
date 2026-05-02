// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A05 — PermissionedMpscChannel::ConsumerHandle exposes only
// try_pop.  Calling try_push on it must be a hard compile error
// because MpscRing's contract is single-consumer-only — silently
// allowing the consumer to also push would race against producers
// AND violate the channel's role discipline.
//
// Expected diagnostic substring (GCC-WRAPPER-TEXT — overload-set
// rejection because try_push is not a member of ConsumerHandle):
//   "no member named .try_push."

#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/permissions/Permission.h>

namespace {

struct BadChan {};

void exercise_consumer_try_push() {
    crucible::concurrent::PermissionedMpscChannel<int, 64, BadChan> ch;
    auto cons_perm = crucible::safety::mint_permission_root<
        crucible::concurrent::mpsc_tag::Consumer<BadChan>>();
    auto consumer = ch.consumer(std::move(cons_perm));

    // CONSUMER attempting PUSH — try_push is structurally absent
    // from ConsumerHandle's API surface.
    consumer.try_push(42);
}

}  // namespace

int main() { exercise_consumer_try_push(); return 0; }
