// ═══════════════════════════════════════════════════════════════════
// test_is_session_handle — sentinel TU for safety/IsSessionHandle.h
//
// Cross-checks `is_session_handle_v<T>` against REAL session-typed
// handles from the `sessions/` tree.  Forces the header through the
// project's full warning matrix.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsSessionHandle.h>

#include <crucible/sessions/Session.h>

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

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

#define EXPECT_TRUE(cond)                                                  \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr,                                           \
                "    EXPECT_TRUE failed: %s (%s:%d)\n",                    \
                #cond, __FILE__, __LINE__);                                \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace extract = ::crucible::safety::extract;
namespace proto   = ::crucible::safety::proto;

// ── Real session-handle witnesses ───────────────────────────────────

struct fake_resource {};

using EndH        = ::crucible::safety::proto::SessionHandle<proto::End, fake_resource>;
using SendIntEndH = ::crucible::safety::proto::SessionHandle<
    proto::Send<int, proto::End>, fake_resource>;
using RecvIntEndH = ::crucible::safety::proto::SessionHandle<
    proto::Recv<int, proto::End>, fake_resource>;

// ── Foreign types ───────────────────────────────────────────────────

struct foreign_struct { int x; };

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_session_handle_smoke_test());
}

void test_negative_cases() {
    static_assert(!extract::is_session_handle_v<int>);
    static_assert(!extract::is_session_handle_v<int*>);
    static_assert(!extract::is_session_handle_v<void>);
    static_assert(!extract::is_session_handle_v<foreign_struct>);
    static_assert(!extract::is_session_handle_v<foreign_struct*>);
}

void test_real_session_handle_end_matches() {
    static_assert(extract::is_session_handle_v<EndH>);
    static_assert(extract::IsSessionHandle<EndH>);
    static_assert(extract::IsSessionHandle<EndH&&>);
    static_assert(extract::IsSessionHandle<EndH const&>);
}

void test_real_session_handle_send_matches() {
    static_assert(extract::is_session_handle_v<SendIntEndH>);
    static_assert(extract::IsSessionHandle<SendIntEndH>);
}

void test_real_session_handle_recv_matches() {
    static_assert(extract::is_session_handle_v<RecvIntEndH>);
    static_assert(extract::IsSessionHandle<RecvIntEndH>);
}

void test_proto_extraction() {
    static_assert(std::is_same_v<
        extract::session_handle_proto_t<EndH>, proto::End>);
    static_assert(std::is_same_v<
        extract::session_handle_proto_t<SendIntEndH>,
        proto::Send<int, proto::End>>);
    static_assert(std::is_same_v<
        extract::session_handle_proto_t<RecvIntEndH>,
        proto::Recv<int, proto::End>>);
}

void test_proto_extraction_cvref_stripped() {
    static_assert(std::is_same_v<
        extract::session_handle_proto_t<EndH&>, proto::End>);
    static_assert(std::is_same_v<
        extract::session_handle_proto_t<EndH const&>, proto::End>);
    static_assert(std::is_same_v<
        extract::session_handle_proto_t<EndH&&>, proto::End>);
}

void test_pointer_to_handle_rejected() {
    // Pointer is not the handle itself.
    static_assert(!extract::is_session_handle_v<EndH*>);
    static_assert(!extract::is_session_handle_v<EndH const*>);
}

void test_distinct_protos_distinguish() {
    static_assert(!std::is_same_v<
        extract::session_handle_proto_t<EndH>,
        extract::session_handle_proto_t<SendIntEndH>>);
    static_assert(!std::is_same_v<
        extract::session_handle_proto_t<SendIntEndH>,
        extract::session_handle_proto_t<RecvIntEndH>>);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_session_handle:\n");
    run_test("test_runtime_smoke",                         test_runtime_smoke);
    run_test("test_negative_cases",                        test_negative_cases);
    run_test("test_real_session_handle_end_matches",       test_real_session_handle_end_matches);
    run_test("test_real_session_handle_send_matches",      test_real_session_handle_send_matches);
    run_test("test_real_session_handle_recv_matches",      test_real_session_handle_recv_matches);
    run_test("test_proto_extraction",                      test_proto_extraction);
    run_test("test_proto_extraction_cvref_stripped",       test_proto_extraction_cvref_stripped);
    run_test("test_pointer_to_handle_rejected",            test_pointer_to_handle_rejected);
    run_test("test_distinct_protos_distinguish",           test_distinct_protos_distinguish);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
