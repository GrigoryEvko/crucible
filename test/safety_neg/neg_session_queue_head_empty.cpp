// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: head_queue_t<EmptyQueue>.  L3 SessionQueue.h's
// HeadQueue primary template fires a dependent_false_v
// static_assert with a clear diagnostic on empty-queue access.

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionQueue.h>

using namespace crucible::safety::proto;

int main() {
    // head_queue_t on EmptyQueue is not allowed — fires static_assert.
    using T = head_queue_t<EmptyQueue>;
    (void)static_cast<T*>(nullptr);
    return 0;
}
