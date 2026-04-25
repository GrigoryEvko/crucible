// Dedicated TU that re-enables every session-type self-test namespace
// (#372 SEPLOG-PERF-2).
//
// The session-type framework headers carry ~1000 cumulative
// static_assert witnesses across 17 *_self_test namespaces.  These
// document the framework's invariants by example AND catch regressions
// during development — but the cost is per-TU compile time, paid by
// every consumer of any Session*.h header.  Per #372, each
// `*_self_test` namespace is now wrapped in
// `#ifdef CRUCIBLE_SESSION_SELF_TESTS` so the cost defaults to zero
// outside this TU.
//
// This TU defines the macro ONCE before including all session
// headers.  It instantiates every static_assert across the family,
// so a regression in any framework invariant fails CI here even
// though no production / test TU pays the per-include cost.
//
// What's still always-on (NOT gated, deliberately):
//   * Session.h's release_size_test (3 sizeof asserts; load-bearing
//     for the zero-cost guarantee — must fire on every release build).
//   * The Send/Recv/Select/Offer/Loop/Continue/End shape examples
//     embedded in Session.h's mpmc_shape_test / req_resp_test /
//     two_pc_test (small, document the canonical shapes).
//   * Wrapper-level static_asserts in non-self-test namespaces
//     (SessionEventLog.h's layout asserts, Refined.h's sizeof
//     guarantees, Checked.h's safe_capacity self-tests, etc.).
//
// Edit the gated namespace's contents, then rebuild this TU to
// catch regressions.  If a self-test fires here but the rest of
// the build is green, fix the framework — don't gate the assert.

#define CRUCIBLE_SESSION_SELF_TESTS 1

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionAssoc.h>
#include <crucible/sessions/SessionCT.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDeclassify.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionDiagnostic.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/SessionPatterns.h>
#include <crucible/sessions/SessionPayloadSubsort.h>
#include <crucible/sessions/SessionQueue.h>
#include <crucible/sessions/SessionSubtype.h>
#include <crucible/sessions/SessionView.h>

#include <cstdio>

int main() {
    // The work happened at compile time.  Reaching this line means
    // every gated static_assert in every session-type self-test
    // namespace fired and passed.
    std::puts("session_self_tests: all framework invariants OK");
    return 0;
}
