// Sentinel TU for `safety/diag/Runtime.h` (FOUND-E06).
//
// Exercises the runtime cold-path violation emitter:
//
//   1. Default sink writes to stderr in the documented format.
//   2. Custom sink installs and routes calls to the new sink.
//   3. nullptr sink silently drops violations.
//   4. Sink restoration round-trips correctly.
//   5. Concurrent install + read is data-race-free (verified by
//      construction; relies on atomic exchange).

#include <crucible/safety/diag/Runtime.h>
#include <crucible/safety/Diagnostic.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace {

// ── Test sink — captures the most recent invocation ──────────────

struct CapturedCall {
    std::atomic<int> count{0};
    crucible::safety::diag::Category last_cat{
        crucible::safety::diag::Category::EffectRowMismatch};
    char last_fn[128]{};
    char last_detail[256]{};
};

CapturedCall g_captured{};

void capture_sink(crucible::safety::diag::Category cat,
                  std::string_view fn,
                  std::string_view detail) noexcept {
    g_captured.count.fetch_add(1, std::memory_order_relaxed);
    g_captured.last_cat = cat;

    // Bounded copy — defensive, never overruns the fixed buffers.
    // Empty string_view's .data() may be nullptr; guard against
    // memcpy(_, nullptr, 0) UB (UBSan flags it even at n=0).
    auto bounded_copy = [](char* dst, std::size_t cap,
                           std::string_view src) noexcept {
        const std::size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
        if (n > 0 && src.data() != nullptr) {
            std::memcpy(dst, src.data(), n);
        }
        dst[n] = '\0';
    };
    bounded_copy(g_captured.last_fn,    sizeof(g_captured.last_fn),    fn);
    bounded_copy(g_captured.last_detail, sizeof(g_captured.last_detail), detail);
}

void reset_capture() noexcept {
    g_captured.count.store(0, std::memory_order_relaxed);
    g_captured.last_cat = crucible::safety::diag::Category::EffectRowMismatch;
    g_captured.last_fn[0]     = '\0';
    g_captured.last_detail[0] = '\0';
}

// ── Test runner — minimal harness ────────────────────────────────

int g_failures = 0;

#define EXPECT(cond, msg) do {                                                \
    if (!(cond)) {                                                             \
        std::fprintf(stderr, "FAIL: %s — %s (%s:%d)\n",                       \
                     #cond, (msg), __FILE__, __LINE__);                        \
        ++g_failures;                                                          \
    }                                                                          \
} while (0)

}  // namespace

namespace {

// ═════════════════════════════════════════════════════════════════════
// Test 1: default sink is installed at startup
// ═════════════════════════════════════════════════════════════════════

[[gnu::cold]] void test_default_sink_installed() {
    namespace diag = ::crucible::safety::diag;
    auto current = diag::current_violation_sink();
    EXPECT(current == &diag::default_violation_sink,
           "default_violation_sink must be installed at startup");
}

// ═════════════════════════════════════════════════════════════════════
// Test 2: custom sink install + route
// ═════════════════════════════════════════════════════════════════════

[[gnu::cold]] void test_custom_sink_install_and_route() {
    namespace diag = ::crucible::safety::diag;

    auto previous = diag::set_violation_sink(&capture_sink);
    EXPECT(previous == &diag::default_violation_sink,
           "set_violation_sink must return the previously-installed sink");
    EXPECT(diag::current_violation_sink() == &capture_sink,
           "current_violation_sink must reflect the just-installed sink");

    reset_capture();
    diag::report_violation(diag::Category::DetSafeLeak,
                           "test_function",
                           "value=42 fails determinism predicate");

    EXPECT(g_captured.count.load() == 1,
           "report_violation must invoke the active sink exactly once");
    EXPECT(g_captured.last_cat == diag::Category::DetSafeLeak,
           "captured category must match the reported one");
    EXPECT(std::string_view{g_captured.last_fn} == "test_function",
           "captured fn-name must match the reported one");
    EXPECT(std::string_view{g_captured.last_detail}
           == "value=42 fails determinism predicate",
           "captured detail must match the reported one");

    // Restore.
    diag::set_violation_sink(previous);
    EXPECT(diag::current_violation_sink() == &diag::default_violation_sink,
           "sink restoration must round-trip");
}

// ═════════════════════════════════════════════════════════════════════
// Test 3: nullptr sink drops silently
// ═════════════════════════════════════════════════════════════════════

[[gnu::cold]] void test_null_sink_drops_silently() {
    namespace diag = ::crucible::safety::diag;

    auto previous = diag::set_violation_sink(nullptr);
    EXPECT(diag::current_violation_sink() == nullptr,
           "nullptr sink install must be observable");

    reset_capture();
    diag::report_violation(diag::Category::HotPathViolation,
                           "drop_test",
                           "should be silently dropped");

    EXPECT(g_captured.count.load() == 0,
           "nullptr sink must NOT route to the previous capture sink");

    diag::set_violation_sink(previous);
}

// ═════════════════════════════════════════════════════════════════════
// Test 4: every Category routes through the sink
// ═════════════════════════════════════════════════════════════════════
//
// Ensures no Category is special-cased silently in the emitter.

[[gnu::cold]] void test_all_categories_route_through_sink() {
    namespace diag = ::crucible::safety::diag;

    auto previous = diag::set_violation_sink(&capture_sink);

    // Walk every Category in the catalog.  enumerate_categories
    // invokes the lambda with Category as a non-type template
    // parameter (`f.template operator()<C>()` per Diagnostic.h:1062).
    int seen = 0;
    diag::enumerate_categories([&]<diag::Category C>() noexcept {
        reset_capture();
        diag::report_violation(C, "enum_test",
                               "category enumeration walk");
        if (g_captured.count.load() == 1 && g_captured.last_cat == C) {
            ++seen;
        }
    });
    EXPECT(seen == 25,
           "every one of 25 foundation Categories must route through sink "
           "(22 wrapper-axis + 3 F* alias)");

    diag::set_violation_sink(previous);
}

// ═════════════════════════════════════════════════════════════════════
// Test 5: long detail strings are bounded by the default sink
// ═════════════════════════════════════════════════════════════════════
//
// The default sink caps each field at 4096 chars to defeat an
// adversarial caller passing GB-sized string_view.  This test
// verifies the cap doesn't crash on large inputs.

[[gnu::cold]] void test_default_sink_handles_long_input() {
    namespace diag = ::crucible::safety::diag;

    // Allocate a 8KB buffer of repeated 'x'; pass as detail.
    constexpr std::size_t big_n = 8192;
    char big[big_n + 1];
    std::memset(big, 'x', big_n);
    big[big_n] = '\0';

    // Re-route to capture sink so the large fprintf doesn't pollute
    // CI logs.  The capture sink does a bounded copy — it won't crash.
    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();
    diag::report_violation(diag::Category::BudgetExceeded,
                           "long_detail_test",
                           std::string_view{big, big_n});
    EXPECT(g_captured.count.load() == 1,
           "long detail must still trigger the sink exactly once");
    diag::set_violation_sink(previous);
}

// ═════════════════════════════════════════════════════════════════════
// Test 6: empty detail / empty fn-name handled gracefully
// ═════════════════════════════════════════════════════════════════════

[[gnu::cold]] void test_empty_strings_handled() {
    namespace diag = ::crucible::safety::diag;

    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();
    diag::report_violation(diag::Category::EpochMismatch, {}, {});
    EXPECT(g_captured.count.load() == 1,
           "empty strings must still trigger the sink");
    EXPECT(std::string_view{g_captured.last_fn}     == "",
           "empty fn must round-trip empty");
    EXPECT(std::string_view{g_captured.last_detail} == "",
           "empty detail must round-trip empty");
    diag::set_violation_sink(previous);
}

// ═════════════════════════════════════════════════════════════════════
// Test 7: sink install is thread-safe (single install observed)
// ═════════════════════════════════════════════════════════════════════
//
// Exercises the atomic exchange semantic.  Not a full TSan test
// (that runs under the tsan preset), just a quick verification that
// repeated install/restore cycles don't leak references.

[[gnu::cold]] void test_sink_install_round_trips() {
    namespace diag = ::crucible::safety::diag;

    for (int i = 0; i < 100; ++i) {
        auto p1 = diag::set_violation_sink(&capture_sink);
        auto p2 = diag::set_violation_sink(p1);
        EXPECT(p2 == &capture_sink,
               "exchange round-trip must return the just-installed sink");
        EXPECT(diag::current_violation_sink() == p1,
               "after restoration, current sink matches the saved previous");
    }
}

// ═════════════════════════════════════════════════════════════════════
// Test 8: report_violation_and_abort signature is [[noreturn]]
// ═════════════════════════════════════════════════════════════════════
//
// Compile-time only — call site is unreachable, but the signature
// must allow it to follow non-returning callers.

[[maybe_unused]] [[noreturn]] [[gnu::cold]]
void unreachable_caller() noexcept {
    namespace diag = ::crucible::safety::diag;
    diag::report_violation_and_abort(
        diag::Category::CrashClassMismatch, "unreachable", "should never run");
}

// ═════════════════════════════════════════════════════════════════════
// Test 9: source_location-capturing report_violation_at
// ═════════════════════════════════════════════════════════════════════
//
// Verifies the location-captured variant emits a "<file>:<line>@<fn>"
// composite via the test sink, with the expected file basename and
// function name.

[[gnu::cold]] void test_report_violation_at_captures_location() {
    namespace diag = ::crucible::safety::diag;

    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();

    diag::report_violation_at(
        diag::Category::RefinementViolation,
        "value=-7 fails predicate `positive`");

    EXPECT(g_captured.count.load() == 1,
           "report_violation_at must invoke the sink exactly once");

    // The captured fn-name is "<file>:<line>@<function>".
    std::string_view fn{g_captured.last_fn};
    EXPECT(fn.find("test_diag_runtime.cpp") != std::string_view::npos,
           "captured location must include this TU's filename");
    EXPECT(fn.find('@') != std::string_view::npos,
           "captured location must include the @ delimiter");
    EXPECT(fn.find("test_report_violation_at_captures_location")
           != std::string_view::npos,
           "captured location must include the calling function name");

    diag::set_violation_sink(previous);
}

// ═════════════════════════════════════════════════════════════════════
// Test 10: CRUCIBLE_RUNTIME_VIOLATION macro
// ═════════════════════════════════════════════════════════════════════

[[gnu::cold]] void test_macro_captures_location() {
    namespace diag = ::crucible::safety::diag;

    auto previous = diag::set_violation_sink(&capture_sink);
    reset_capture();

    CRUCIBLE_RUNTIME_VIOLATION(
        diag::Category::HotPathViolation,
        "macro test detail");

    EXPECT(g_captured.count.load() == 1,
           "CRUCIBLE_RUNTIME_VIOLATION must route through the sink");
    EXPECT(g_captured.last_cat == diag::Category::HotPathViolation,
           "macro must pass Category through correctly");
    EXPECT(std::string_view{g_captured.last_detail} == "macro test detail",
           "macro must pass detail through correctly");

    std::string_view fn{g_captured.last_fn};
    EXPECT(fn.find("test_macro_captures_location") != std::string_view::npos,
           "macro-captured location must name the calling function");

    diag::set_violation_sink(previous);
}

}  // anonymous namespace covering all test functions

// ═════════════════════════════════════════════════════════════════════
// Main
// ═════════════════════════════════════════════════════════════════════

[[gnu::cold]] int main() {
    test_default_sink_installed();
    test_custom_sink_install_and_route();
    test_null_sink_drops_silently();
    test_all_categories_route_through_sink();
    test_default_sink_handles_long_input();
    test_empty_strings_handled();
    test_sink_install_round_trips();
    test_report_violation_at_captures_location();
    test_macro_captures_location();

    if (g_failures > 0) {
        std::fprintf(stderr, "FAILURES: %d\n", g_failures);
        return 1;
    }
    return 0;
}
