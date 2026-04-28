// ═══════════════════════════════════════════════════════════════════
// test_row_mismatch_compile — sentinel TU for safety/diag/RowMismatch.h
//
// Same blind-spot rationale as test_diagnostic_compile / test_stable_
// name_compile.  Forces RowMismatch.h through the test target's full
// -Werror matrix and exercises the consteval message builder under
// non-constant args.
//
// Coverage:
//   * Foundation header inclusion under full warning flags.
//   * type_name<T> aliases stable_name_of<T> correctly.
//   * function_display_name<F> non-empty + reflective.
//   * build_row_mismatch_message<...> produces the expected 8-line
//     format (line-prefix substring matching at runtime).
//   * row_mismatch_message_v<...> caches and returns identical buffer.
//   * CRUCIBLE_ROW_MISMATCH_ASSERT macro happy-path compile (true
//     condition: assertion passes silently).
//   * Format version constant.
//   * runtime_smoke_test_row_mismatch execution.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/safety/diag/RowMismatch.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>

// Namespace-scope sample functions for function_display_name tests.
inline void sample_dispatch(int)             noexcept {}
inline int  sample_compute(int, float)       noexcept { return 0; }

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

#define EXPECT_EQ(a, b)                                                    \
    do {                                                                   \
        if (!((a) == (b))) {                                               \
            std::fprintf(stderr,                                           \
                "    EXPECT_EQ failed: %s == %s (%s:%d)\n",                \
                #a, #b, __FILE__, __LINE__);                               \
            throw TestFailure{};                                           \
        }                                                                  \
    } while (0)

namespace diag = ::crucible::safety::diag;

// ─── Tests ──────────────────────────────────────────────────────────

void test_runtime_smoke() {
    diag::runtime_smoke_test_row_mismatch();
}

void test_type_name_aliases_stable_name_of() {
    static_assert(diag::type_name<int>   == diag::stable_name_of<int>);
    static_assert(diag::type_name<float> == diag::stable_name_of<float>);

    EXPECT_EQ(diag::type_name<int>,   diag::stable_name_of<int>);
    EXPECT_EQ(diag::type_name<void>,  diag::stable_name_of<void>);
    EXPECT_TRUE(diag::type_name<int>.ends_with("int"));
}

void test_function_display_name_non_empty() {
    constexpr auto n1 = diag::function_display_name<&::sample_dispatch>;
    constexpr auto n2 = diag::function_display_name<&::sample_compute>;
    static_assert(!n1.empty());
    static_assert(!n2.empty());

    EXPECT_TRUE(!n1.empty());
    EXPECT_TRUE(!n2.empty());

    // Different functions → different display names.
    EXPECT_TRUE(n1 != n2);
}

void test_format_version() {
    static_assert(diag::CRUCIBLE_DIAG_FORMAT_VERSION == 1);
    EXPECT_EQ(diag::CRUCIBLE_DIAG_FORMAT_VERSION, std::size_t{1});
}

void test_message_builder_format_shape() {
    // libstdc++ 16's `string_view::find` (and therefore `contains` /
    // `starts_with` / `ends_with`) uses pointer arithmetic inside
    // bits/string_view.tcc that isn't constexpr-safe through
    // std::array<char, N>::data().  Run substring checks at RUNTIME;
    // the consteval-evaluable invariants (length > 0, length match,
    // first/last char) are verified in the header self-test block.

    static constexpr auto msg = diag::build_row_mismatch_message<
        diag::EffectRowMismatch, &::sample_dispatch,
        int, float, double>();

    std::string_view const view = msg.view();

    EXPECT_TRUE(view.starts_with("[EffectRowMismatch]"));
    EXPECT_TRUE(view.ends_with("\n"));
    EXPECT_TRUE(view.contains("\n  at "));
    EXPECT_TRUE(view.contains("\n  caller row contains: "));
    EXPECT_TRUE(view.contains("\n  callee requires:     Subrow<_, "));
    EXPECT_TRUE(view.contains("\n  offending atoms:     "));
    EXPECT_TRUE(view.contains("\n  remediation: "));
    EXPECT_TRUE(view.contains("\n  docs: "));

    // 7 newlines for 7 lines.
    std::size_t newlines = 0;
    for (char c : view) {
        if (c == '\n') ++newlines;
    }
    EXPECT_EQ(newlines, std::size_t{7});
}

void test_message_builder_per_category() {
    static constexpr auto msg_a = diag::build_row_mismatch_message<
        diag::HotPathViolation, &::sample_dispatch,
        int, float, double>();
    static constexpr auto msg_b = diag::build_row_mismatch_message<
        diag::DetSafeLeak, &::sample_dispatch,
        int, float, double>();

    std::string_view const view_a = msg_a.view();
    std::string_view const view_b = msg_b.view();

    EXPECT_TRUE(view_a.starts_with("[HotPathViolation]"));
    EXPECT_TRUE(view_b.starts_with("[DetSafeLeak]"));

    // Different categories → different remediation text.
    EXPECT_TRUE(view_a != view_b);
}

void test_row_mismatch_message_v_caching() {
    constexpr auto& cached1 = diag::row_mismatch_message_v<
        diag::EffectRowMismatch, &::sample_dispatch, int, float, double>;
    constexpr auto& cached2 = diag::row_mismatch_message_v<
        diag::EffectRowMismatch, &::sample_dispatch, int, float, double>;

    // Same template instantiation → same variable (linker collapses
    // inline constexpr to one definition).
    EXPECT_EQ(&cached1, &cached2);

    // Compile-time-checkable: first character of buffer is '['.
    static_assert(cached1.data[std::size_t{0}] == '[');
    EXPECT_EQ(cached1.data[std::size_t{0}], '[');

    // Runtime substring check.
    EXPECT_TRUE(cached1.view().starts_with("[EffectRowMismatch]"));
}

void test_macro_happy_path() {
    // Macro invocation with true condition: silent compile.
    CRUCIBLE_ROW_MISMATCH_ASSERT(
        true,
        EffectRowMismatch,
        &::sample_dispatch,
        int,
        float,
        double);

    CRUCIBLE_ROW_MISMATCH_ASSERT(
        (std::is_same_v<int, int>),
        HotPathViolation,
        &::sample_compute,
        long,
        short,
        char);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_row_mismatch_compile:\n");
    run_test("test_runtime_smoke",                       test_runtime_smoke);
    run_test("test_type_name_aliases_stable_name_of",    test_type_name_aliases_stable_name_of);
    run_test("test_function_display_name_non_empty",     test_function_display_name_non_empty);
    run_test("test_format_version",                      test_format_version);
    run_test("test_message_builder_format_shape",        test_message_builder_format_shape);
    run_test("test_message_builder_per_category",        test_message_builder_per_category);
    run_test("test_row_mismatch_message_v_caching",      test_row_mismatch_message_v_caching);
    run_test("test_macro_happy_path",                    test_macro_happy_path);
    std::fprintf(stderr, "\n%d passed, %d failed\n",
                 total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
