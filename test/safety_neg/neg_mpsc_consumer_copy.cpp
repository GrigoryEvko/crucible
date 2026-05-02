// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-A05 — PermissionedMpscChannel::ConsumerHandle is move-only.
// Copying it would duplicate the linear Permission<Consumer> token
// and silently allow two consumers to coexist on the same channel,
// breaking MpscRing's single-consumer contract.
//
// Expected diagnostic substring (FRAMEWORK-CONTROLLED — taken from
// ConsumerHandle's own = delete reason string):
//   "ConsumerHandle owns the Consumer Permission"

#include <crucible/concurrent/PermissionedMpscChannel.h>
#include <crucible/permissions/Permission.h>

namespace {

struct BadChan {};

void exercise_consumer_copy() {
    crucible::concurrent::PermissionedMpscChannel<int, 64, BadChan> ch;
    auto cons_perm = crucible::safety::mint_permission_root<
        crucible::concurrent::mpsc_tag::Consumer<BadChan>>();
    auto consumer = ch.consumer(std::move(cons_perm));

    // Attempt to copy — deleted with reason.
    auto consumer2 = consumer;
    (void)consumer2;
}

}  // namespace

int main() { exercise_consumer_copy(); return 0; }
