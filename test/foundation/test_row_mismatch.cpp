// Including the header from a translation unit is what drives it
// through the target's full warning matrix.  The body then exercises
// the consteval message builder with non-constant arguments.
//
// Old spelling: test/test_row_mismatch_compile.cpp.  The runtime smoke
// test that the old header carried inline, compiled into every
// including translation unit and called only from here, is the first
// case below.

#include <foundation/diag/RowMismatch.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>

// The display-name tests need functions at namespace scope.
inline void sample_dispatch(int) noexcept {}
inline int sample_compute(int, float) noexcept { return 0; }
inline void smoke_fn(int) noexcept {}

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

#define EXPECT_TRUE(cond)                                                                            \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            std::fprintf(stderr, "    EXPECT_TRUE failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            throw TestFailure{};                                                                     \
        }                                                                                            \
    } while (0)

#define EXPECT_EQ(a, b)                                                                                   \
    do {                                                                                                  \
        if (!((a) == (b))) {                                                                              \
            std::fprintf(stderr, "    EXPECT_EQ failed: %s == %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
            throw TestFailure{};                                                                          \
        }                                                                                                 \
    } while (0)

namespace diag = ::foundation::diag;
namespace refl = ::foundation::reflect;

// A static_assert can be discharged without the consteval body running
// as written, so the same surface is consumed here from a runtime
// context through volatile sinks the optimizer cannot fold away.
void test_runtime_smoke() {
    constexpr auto msg = diag::row_mismatch_message_v<diag::EffectRowMismatch, &::smoke_fn, int, float, double>;

    // The signedness of char is implementation-defined, so each byte is
    // cast to unsigned before it reaches the exclusive-or.
    volatile std::size_t sink = msg.length;
    auto const first = static_cast<unsigned char>(msg.data[std::size_t{0}]);
    sink ^= first;
    if (msg.length > 1) {
        auto const last = static_cast<unsigned char>(msg.data[msg.length - 1]);
        sink ^= last;
    }
    (void)sink;

    volatile std::size_t name_sink = diag::type_name<int>.size();
    name_sink ^= diag::type_name<float>.size();
    (void)name_sink;

    volatile std::size_t fn_sink = diag::function_display_name<&::smoke_fn>.size();
    (void)fn_sink;
}

void test_type_name_aliases_stable_name_of() {
    static_assert(diag::type_name<int> == refl::stable_name_of<int>);
    static_assert(diag::type_name<float> == refl::stable_name_of<float>);

    EXPECT_EQ(diag::type_name<int>, refl::stable_name_of<int>);
    EXPECT_EQ(diag::type_name<void>, refl::stable_name_of<void>);
    EXPECT_TRUE(diag::type_name<int>.ends_with("int"));
}

void test_function_display_name_non_empty() {
    constexpr auto n1 = diag::function_display_name<&::sample_dispatch>;
    constexpr auto n2 = diag::function_display_name<&::sample_compute>;
    static_assert(!n1.empty());
    static_assert(!n2.empty());

    EXPECT_TRUE(!n1.empty());
    EXPECT_TRUE(!n2.empty());

    EXPECT_TRUE(n1 != n2);

    // The extracted name must carry the function's own identifier, not
    // just its pointer-type signature.  The search runs at runtime
    // because string_view::find is not constexpr-safe over a view
    // backed by a static array.
    std::fprintf(stderr,
                 "\n      [function_display_name<&sample_dispatch>] = %.*s\n"
                 "      [function_display_name<&sample_compute>]  = %.*s\n      ",
                 static_cast<int>(n1.size()), n1.data(), static_cast<int>(n2.size()), n2.data());
    EXPECT_TRUE(n1.find("sample_dispatch") != std::string_view::npos);
    EXPECT_TRUE(n2.find("sample_compute") != std::string_view::npos);
}

void test_format_version() {
    static_assert(diag::CRUCIBLE_DIAG_FORMAT_VERSION == 1);
    EXPECT_EQ(diag::CRUCIBLE_DIAG_FORMAT_VERSION, std::size_t{1});
}

void test_message_builder_format_shape() {
    // The compile-time checks index the buffer through the detail
    // helpers rather than calling string_view::find, which libstdc++ 16
    // cannot evaluate in a constant expression.  The same checks run
    // through the ordinary string_view API below, because the defect is
    // constexpr-only.

    static constexpr auto msg =
        diag::build_row_mismatch_message<diag::EffectRowMismatch, &::sample_dispatch, int, float, double>();

    static_assert(diag::detail::buffer_starts_with(msg, "[EffectRowMismatch]"));
    static_assert(diag::detail::buffer_ends_with(msg, "\n"));
    static_assert(diag::detail::buffer_count_char(msg, '\n') == diag::CRUCIBLE_DIAG_FORMAT_LINES);

    std::string_view const view = msg.view();
    EXPECT_TRUE(view.starts_with("[EffectRowMismatch]"));
    EXPECT_TRUE(view.ends_with("\n"));
    EXPECT_TRUE(view.contains("\n  at "));
    EXPECT_TRUE(view.contains("\n  caller row contains: "));
    EXPECT_TRUE(view.contains("\n  callee requires:     Subrow<_, "));
    EXPECT_TRUE(view.contains("\n  offending atoms:     "));
    EXPECT_TRUE(view.contains("\n  remediation: "));
    EXPECT_TRUE(view.contains("\n  docs: "));

    std::size_t newlines = 0;
    for (char c : view) {
        if (c == '\n') ++newlines;
    }
    EXPECT_EQ(newlines, std::size_t{diag::CRUCIBLE_DIAG_FORMAT_LINES});
}

void test_message_builder_per_category() {
    static constexpr auto msg_a =
        diag::build_row_mismatch_message<diag::HotPathViolation, &::sample_dispatch, int, float, double>();
    static constexpr auto msg_b =
        diag::build_row_mismatch_message<diag::DetSafeLeak, &::sample_dispatch, int, float, double>();

    std::string_view const view_a = msg_a.view();
    std::string_view const view_b = msg_b.view();

    EXPECT_TRUE(view_a.starts_with("[HotPathViolation]"));
    EXPECT_TRUE(view_b.starts_with("[DetSafeLeak]"));

    // The category selects the remediation text as well as the tag.
    EXPECT_TRUE(view_a != view_b);
}

void test_row_mismatch_message_v_caching() {
    constexpr auto& cached1 =
        diag::row_mismatch_message_v<diag::EffectRowMismatch, &::sample_dispatch, int, float, double>;
    constexpr auto& cached2 =
        diag::row_mismatch_message_v<diag::EffectRowMismatch, &::sample_dispatch, int, float, double>;

    // One address for both means the linker collapsed the inline
    // constexpr to a single definition rather than rebuilding the
    // message per use.
    EXPECT_EQ(&cached1, &cached2);

    static_assert(cached1.data[std::size_t{0}] == '[');
    EXPECT_EQ(cached1.data[std::size_t{0}], '[');

    EXPECT_TRUE(cached1.view().starts_with("[EffectRowMismatch]"));
}

void test_macro_happy_path() {
    // A true condition must compile silently, so the absence of a
    // diagnostic is the whole claim here.
    CRUCIBLE_ROW_MISMATCH_ASSERT(true, EffectRowMismatch, &::sample_dispatch, int, float, double);

    CRUCIBLE_ROW_MISMATCH_ASSERT((std::is_same_v<int, int>), HotPathViolation, &::sample_compute, long, short, char);
}

}  // namespace

int main() {
    std::fprintf(stderr, "test_row_mismatch:\n");
    run_test("test_runtime_smoke", test_runtime_smoke);
    run_test("test_type_name_aliases_stable_name_of", test_type_name_aliases_stable_name_of);
    run_test("test_function_display_name_non_empty", test_function_display_name_non_empty);
    run_test("test_format_version", test_format_version);
    run_test("test_message_builder_format_shape", test_message_builder_format_shape);
    run_test("test_message_builder_per_category", test_message_builder_per_category);
    run_test("test_row_mismatch_message_v_caching", test_row_mismatch_message_v_caching);
    run_test("test_macro_happy_path", test_macro_happy_path);
    std::fprintf(stderr, "\n%d passed, %d failed\n", total_passed, total_failed);
    if (total_failed > 0) return EXIT_FAILURE;
    std::fprintf(stderr, "ALL PASSED\n");
    return EXIT_SUCCESS;
}
