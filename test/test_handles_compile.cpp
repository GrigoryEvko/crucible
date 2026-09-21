// Sentinel TU: compiles every handles header under the test target warning
// matrix so their static_asserts run.

#include <crucible/handles/FileHandle.h>
#include <crucible/handles/Handles.h>
#include <crucible/handles/LazyEstablishedChannel.h>
#include <crucible/handles/_Once.h>
#include <crucible/handles/OneShotFlag.h>
#include <crucible/handles/PublishOnce.h>

#include <cstdio>
#include <cstdlib>
#include <expected>
#include <system_error>
#include <type_traits>

// The error signal must travel in the return type, not in an is_open() flag on
// a bare FileHandle that discards errno.  These assertions pin that contract, so
// a factory signature reverted to returning a bare handle is caught here.
namespace file_handle_contract {

using ::crucible::safety::FileHandle;
using ExpectedFh = std::expected<FileHandle, std::error_code>;

static_assert(std::is_same_v<decltype(::crucible::safety::open_read(static_cast<const char*>(nullptr))), ExpectedFh>);
static_assert(
    std::is_same_v<decltype(::crucible::safety::open_write_truncate(static_cast<const char*>(nullptr))), ExpectedFh>);
static_assert(
    std::is_same_v<decltype(::crucible::safety::open_write_append(static_cast<const char*>(nullptr))), ExpectedFh>);

}  // namespace file_handle_contract

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

void test_file_handle_compile() {}
void test_handles_umbrella() {}
void test_lazy_established_channel_compile() {}
void test_once_compile() {}
void test_one_shot_flag_compile() {}
void test_publish_once_compile() {}

}  // namespace

int main() {
    std::fprintf(stderr, "test_handles_compile:\n");
    run_test("test_file_handle_compile", test_file_handle_compile);
    run_test("test_handles_umbrella", test_handles_umbrella);
    run_test("test_lazy_established_channel_compile", test_lazy_established_channel_compile);
    run_test("test_once_compile", test_once_compile);
    run_test("test_one_shot_flag_compile", test_one_shot_flag_compile);
    run_test("test_publish_once_compile", test_publish_once_compile);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
