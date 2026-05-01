// ═══════════════════════════════════════════════════════════════════
// test_sessions_compile — sentinel TU for sessions/* tree
//
// Same blind-spot rationale as test_algebra_compile / test_safety_
// compile (see feedback_header_only_static_assert_blind_spot memory).
// Forces every sessions/* header through the test target's full
// -Werror matrix.
//
// Coverage: 21 headers in sessions/.  Note: Many already have deeper
// dedicated tests (test_session_*); this sentinel does NOT duplicate
// them.  Its job is to guarantee the EMBEDDED static_assert blocks
// in EVERY header fire under the test target's flags — catches the
// same -Wshadow / -Wswitch-default / display_string_of fragility
// patterns that surfaced for algebra/* during MIGRATE-2.
//
// When a new sessions/* header ships, add its include below.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionAssoc.h>
#include <crucible/sessions/SessionCheckpoint.h>
#include <crucible/sessions/SessionContentAddressed.h>
#include <crucible/sessions/SessionContext.h>
#include <crucible/sessions/SessionCT.h>
#include <crucible/sessions/SessionCrash.h>
#include <crucible/sessions/SessionDeclassify.h>
#include <crucible/sessions/SessionDelegate.h>
#include <crucible/sessions/SessionDiagnostic.h>
#include <crucible/sessions/SessionEventLog.h>
#include <crucible/sessions/SessionGlobal.h>
#include <crucible/sessions/PermissionedSession.h>
#include <crucible/sessions/SessionPatterns.h>
#include <crucible/sessions/SessionPayloadSubsort.h>
#include <crucible/sessions/SessionPermPayloads.h>
#include <crucible/sessions/SessionQueue.h>
#include <crucible/sessions/SessionRowExtraction.h>
#include <crucible/sessions/SessionSubtype.h>
#include <crucible/sessions/SessionSubtypeReason.h>
#include <crucible/sessions/SessionView.h>
#include <crucible/sessions/Sessions.h>

#include <cstdio>
#include <cstdlib>

namespace {

struct TestFailure {};
int total_passed = 0;
int total_failed = 0;

template <typename F>
void run_test(const char* name, F&& body) {
    std::fprintf(stderr, "  %s: ", name);
    try {
        body();
        ++total_passed;
        std::fprintf(stderr, "PASSED\n");
    } catch (TestFailure&) {
        ++total_failed;
        std::fprintf(stderr, "FAILED\n");
    }
}

// One probe per header file.  Body-empty by design.
void test_session_compile()                  {}
void test_session_assoc_compile()            {}
void test_session_checkpoint_compile()       {}
void test_session_content_addressed_compile(){}
void test_session_context_compile()          {}
void test_session_ct_compile()               {}
void test_session_crash_compile()            {}
void test_session_declassify_compile()       {}
void test_session_delegate_compile()         {}
void test_session_diagnostic_compile()       {}
void test_session_event_log_compile()        {}
void test_session_global_compile()           {}
void test_session_patterns_compile()         {}
void test_session_payload_subsort_compile()  {}
void test_session_perm_payloads_compile() {
    crucible::safety::proto::detail::session_perm_payloads_smoke::runtime_smoke_test();
}
void test_permissioned_session_compile() {
    crucible::safety::proto::detail::permissioned_session_smoke::runtime_smoke_test();
}
void test_session_queue_compile()            {}
void test_session_row_extraction_compile() {
    crucible::sessions::runtime_smoke_test_payload_row();
}
void test_session_subtype_compile()          {}
void test_session_subtype_reason_compile()   {}
void test_session_view_compile()             {}
void test_sessions_umbrella()                {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_sessions_compile:\n");
    run_test("test_session_compile",                  test_session_compile);
    run_test("test_session_assoc_compile",            test_session_assoc_compile);
    run_test("test_session_checkpoint_compile",       test_session_checkpoint_compile);
    run_test("test_session_content_addressed_compile",test_session_content_addressed_compile);
    run_test("test_session_context_compile",          test_session_context_compile);
    run_test("test_session_ct_compile",               test_session_ct_compile);
    run_test("test_session_crash_compile",            test_session_crash_compile);
    run_test("test_session_declassify_compile",       test_session_declassify_compile);
    run_test("test_session_delegate_compile",         test_session_delegate_compile);
    run_test("test_session_diagnostic_compile",       test_session_diagnostic_compile);
    run_test("test_session_event_log_compile",        test_session_event_log_compile);
    run_test("test_session_global_compile",           test_session_global_compile);
    run_test("test_session_patterns_compile",         test_session_patterns_compile);
    run_test("test_session_payload_subsort_compile",  test_session_payload_subsort_compile);
    run_test("test_session_perm_payloads_compile",    test_session_perm_payloads_compile);
    run_test("test_permissioned_session_compile",     test_permissioned_session_compile);
    run_test("test_session_queue_compile",            test_session_queue_compile);
    run_test("test_session_row_extraction_compile",   test_session_row_extraction_compile);
    run_test("test_session_subtype_compile",          test_session_subtype_compile);
    run_test("test_session_subtype_reason_compile",   test_session_subtype_reason_compile);
    run_test("test_session_view_compile",             test_session_view_compile);
    run_test("test_sessions_umbrella",                test_sessions_umbrella);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
