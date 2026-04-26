// ═══════════════════════════════════════════════════════════════════
// test_bridges_compile — sentinel TU for bridges/* tree
//
// Same blind-spot rationale as test_algebra_compile / test_safety_
// compile (see feedback_header_only_static_assert_blind_spot memory).
// Forces every bridges/* header through the test target's full
// -Werror matrix.
//
// Coverage: 4 headers (Bridges, CrashTransport, MachineSessionBridge,
// RecordingSessionHandle).  When a new bridges/* header ships, add
// its include below.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/bridges/Bridges.h>
#include <crucible/bridges/CrashTransport.h>
#include <crucible/bridges/MachineSessionBridge.h>
#include <crucible/bridges/RecordingSessionHandle.h>

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

void test_bridges_umbrella()                  {}
void test_crash_transport_compile()           {}
void test_machine_session_bridge_compile()    {}
void test_recording_session_handle_compile()  {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_bridges_compile:\n");
    run_test("test_bridges_umbrella",                  test_bridges_umbrella);
    run_test("test_crash_transport_compile",           test_crash_transport_compile);
    run_test("test_machine_session_bridge_compile",    test_machine_session_bridge_compile);
    run_test("test_recording_session_handle_compile",  test_recording_session_handle_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
