// ═══════════════════════════════════════════════════════════════════
// test_is_swmr_handle — sentinel TU for safety/IsSwmrHandle.h
//
// Cross-checks `is_swmr_writer_v` / `is_swmr_reader_v` against REAL
// PermissionedSnapshot::WriterHandle / ReaderHandle and verifies the
// mutual exclusion (a writer is rejected by the reader trait, and
// vice versa).
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/IsSwmrHandle.h>

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/permissions/Permission.h>

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
namespace concur  = ::crucible::concurrent;
namespace safety  = ::crucible::safety;

// ── Synthetic witnesses ─────────────────────────────────────────────

struct synth_writer {
    void publish(int const&) noexcept {}
};

struct synth_reader {
    [[nodiscard]] int load() const noexcept { return 0; }
};

struct synth_hybrid {
    void publish(int const&) noexcept {}
    [[nodiscard]] int load() const noexcept { return 0; }
};

struct synth_bool_publish {
    [[nodiscard]] bool publish(int const&) noexcept { return true; }
};

struct synth_non_const_load {
    [[nodiscard]] int load() noexcept { return 0; }
};

struct synth_void_load {
    void load() const noexcept {}
};

// ── Real-world cross-check setup ────────────────────────────────────

struct cross_check_tag {};
using Snap = concur::PermissionedSnapshot<int, cross_check_tag>;

void test_runtime_smoke() {
    EXPECT_TRUE(extract::is_swmr_handle_smoke_test());
}

void test_synthetic_writer_positive() {
    static_assert(extract::is_swmr_writer_v<synth_writer>);
    static_assert(extract::IsSwmrWriter<synth_writer>);
    static_assert(extract::is_swmr_writer_v<synth_writer&>);
    static_assert(extract::is_swmr_writer_v<synth_writer const&>);
}

void test_synthetic_writer_negative() {
    static_assert(!extract::is_swmr_writer_v<int>);
    static_assert(!extract::is_swmr_writer_v<void>);
    static_assert(!extract::is_swmr_writer_v<synth_reader>);
    static_assert(!extract::is_swmr_writer_v<synth_hybrid>);
    static_assert(!extract::is_swmr_writer_v<synth_bool_publish>);
    static_assert(!extract::is_swmr_writer_v<synth_writer*>);
}

void test_synthetic_reader_positive() {
    static_assert(extract::is_swmr_reader_v<synth_reader>);
    static_assert(extract::IsSwmrReader<synth_reader>);
    static_assert(extract::is_swmr_reader_v<synth_reader&>);
    static_assert(extract::is_swmr_reader_v<synth_reader const&>);
}

void test_synthetic_reader_negative() {
    static_assert(!extract::is_swmr_reader_v<int>);
    static_assert(!extract::is_swmr_reader_v<void>);
    static_assert(!extract::is_swmr_reader_v<synth_writer>);
    static_assert(!extract::is_swmr_reader_v<synth_hybrid>);
    static_assert(!extract::is_swmr_reader_v<synth_non_const_load>);
    static_assert(!extract::is_swmr_reader_v<synth_void_load>);
    static_assert(!extract::is_swmr_reader_v<synth_reader*>);
}

void test_payload_extraction() {
    static_assert(std::is_same_v<
        extract::swmr_writer_value_t<synth_writer>, int>);
    static_assert(std::is_same_v<
        extract::swmr_writer_value_t<synth_writer const&>, int>);
    static_assert(std::is_same_v<
        extract::swmr_reader_value_t<synth_reader>, int>);
    static_assert(std::is_same_v<
        extract::swmr_reader_value_t<synth_reader const&>, int>);
}

void test_real_snapshot_writer_handle_matches() {
    using WH = Snap::WriterHandle;
    static_assert(extract::is_swmr_writer_v<WH>);
    static_assert(extract::IsSwmrWriter<WH>);
}

void test_real_snapshot_reader_handle_matches() {
    using RH = Snap::ReaderHandle;
    static_assert(extract::is_swmr_reader_v<RH>);
    static_assert(extract::IsSwmrReader<RH>);
}

void test_real_snapshot_writer_rejected_by_reader_trait() {
    using WH = Snap::WriterHandle;
    static_assert(!extract::is_swmr_reader_v<WH>);
}

void test_real_snapshot_reader_rejected_by_writer_trait() {
    using RH = Snap::ReaderHandle;
    static_assert(!extract::is_swmr_writer_v<RH>);
}

void test_real_payload_extraction() {
    using WH = Snap::WriterHandle;
    using RH = Snap::ReaderHandle;
    static_assert(std::is_same_v<
        extract::swmr_writer_value_t<WH>, int>);
    static_assert(std::is_same_v<
        extract::swmr_reader_value_t<RH>, int>);
}

void test_runtime_round_trip() {
    Snap snap{42};
    auto wperm = safety::mint_permission_root<typename Snap::writer_tag>();
    auto w = snap.writer(std::move(wperm));
    w.publish(99);
    auto rh_opt = snap.reader();
    EXPECT_TRUE(rh_opt.has_value());
    EXPECT_TRUE(rh_opt->load() == 99);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_is_swmr_handle:\n");
    run_test("test_runtime_smoke",                        test_runtime_smoke);
    run_test("test_synthetic_writer_positive",            test_synthetic_writer_positive);
    run_test("test_synthetic_writer_negative",            test_synthetic_writer_negative);
    run_test("test_synthetic_reader_positive",            test_synthetic_reader_positive);
    run_test("test_synthetic_reader_negative",            test_synthetic_reader_negative);
    run_test("test_payload_extraction",                   test_payload_extraction);
    run_test("test_real_snapshot_writer_handle_matches",  test_real_snapshot_writer_handle_matches);
    run_test("test_real_snapshot_reader_handle_matches",  test_real_snapshot_reader_handle_matches);
    run_test("test_real_snapshot_writer_rejected_by_reader_trait",
                                                          test_real_snapshot_writer_rejected_by_reader_trait);
    run_test("test_real_snapshot_reader_rejected_by_writer_trait",
                                                          test_real_snapshot_reader_rejected_by_writer_trait);
    run_test("test_real_payload_extraction",              test_real_payload_extraction);
    run_test("test_runtime_round_trip",                   test_runtime_round_trip);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
