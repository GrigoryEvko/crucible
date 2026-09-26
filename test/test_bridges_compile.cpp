// Sentinel TU: compiles every bridges header under the test target warning
// matrix so their static_asserts run.

// _SessionPersistence.h has inline bodies that need the full Cipher class,
// and it does not include Cipher.h itself.
#include <crucible/Cipher.h>
#include <crucible/bridges/_CrashTransport.h>
#include <crucible/bridges/_EndpointMint.h>
#include <crucible/bridges/_MachineSessionBridge.h>
#include <crucible/bridges/_RecordingSessionHandle.h>
#include <crucible/bridges/_SessionPersistence.h>

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

void test_crash_transport_compile() {}
void test_machine_session_bridge_compile() {}
void test_recording_session_handle_compile() {}
void test_endpoint_mint_compile() {}
void test_session_persistence_compile() {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_bridges_compile:\n");
    run_test("test_crash_transport_compile", test_crash_transport_compile);
    run_test("test_machine_session_bridge_compile", test_machine_session_bridge_compile);
    run_test("test_recording_session_handle_compile", test_recording_session_handle_compile);
    run_test("test_endpoint_mint_compile", test_endpoint_mint_compile);
    run_test("test_session_persistence_compile", test_session_persistence_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
