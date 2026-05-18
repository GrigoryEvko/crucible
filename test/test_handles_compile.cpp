// ═══════════════════════════════════════════════════════════════════
// test_handles_compile — sentinel TU for handles/* tree
//
// Same blind-spot rationale as test_algebra_compile / test_safety_
// compile (see feedback_header_only_static_assert_blind_spot memory).
// Forces every handles/* header through the test target's full
// -Werror matrix.
//
// Coverage: 6 headers (FileHandle, Handles, LazyEstablishedChannel,
// Once, OneShotFlag, PublishOnce).  When a new handles/* header
// ships, add its include below.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/handles/FileHandle.h>
#include <crucible/handles/Handles.h>
#include <crucible/handles/LazyEstablishedChannel.h>
#include <crucible/handles/Once.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/handles/PublishOnce.h>

#include <cstdio>
#include <cstdlib>
#include <expected>
#include <system_error>
#include <type_traits>

// ─────────────────────────────────────────────────────────────────────
// fixy-A1-013 sentinel: pin Handles.h's documented factory return shape
// to FileHandle.h's actual return shape via the type system.  Pre-fix
// the umbrella doc lied — claimed `std::expected<T, SyscallError>` while
// the factories returned a bare FileHandle whose `is_open()` carried
// the error signal and the real errno was discarded.  Post-fix every
// open_* factory returns `std::expected<FileHandle, std::error_code>`,
// and this static_assert wedges that contract into the type system so
// future drift (e.g. someone "fixing" a build by reverting the factory
// signature) is caught at the umbrella-test layer.
// ─────────────────────────────────────────────────────────────────────
namespace fixy_a1_013 {

using ::crucible::safety::FileHandle;
using ExpectedFh = std::expected<FileHandle, std::error_code>;

static_assert(std::is_same_v<
    decltype(::crucible::safety::open_read(static_cast<const char*>(nullptr))),
    ExpectedFh>);
static_assert(std::is_same_v<
    decltype(::crucible::safety::open_write_truncate(
        static_cast<const char*>(nullptr))),
    ExpectedFh>);
static_assert(std::is_same_v<
    decltype(::crucible::safety::open_write_append(
        static_cast<const char*>(nullptr))),
    ExpectedFh>);

}  // namespace fixy_a1_013

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

void test_file_handle_compile()              {}
void test_handles_umbrella()                 {}
void test_lazy_established_channel_compile() {}
void test_once_compile()                     {}
void test_one_shot_flag_compile()            {}
void test_publish_once_compile()             {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_handles_compile:\n");
    run_test("test_file_handle_compile",              test_file_handle_compile);
    run_test("test_handles_umbrella",                 test_handles_umbrella);
    run_test("test_lazy_established_channel_compile", test_lazy_established_channel_compile);
    run_test("test_once_compile",                     test_once_compile);
    run_test("test_one_shot_flag_compile",            test_one_shot_flag_compile);
    run_test("test_publish_once_compile",             test_publish_once_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
