// Including the header from a translation unit is what drives it
// through the target's full warning matrix.  The body then exercises
// the consteval message builder with non-constant arguments.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/diag/RowMismatch.h>
#include <foundation/diag/tags/Rows.h>
#include <foundation/diag/tags/Tiers.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <type_traits>

// The display-name tests need functions at namespace scope.
inline void sample_dispatch(int) noexcept {}
inline int sample_compute(int, float) noexcept { return 0; }
inline void smoke_fn(int) noexcept {}

namespace {

using ::fixy::ends_with;
using ::fixy::find;
using ::fixy::starts_with;
using ::fixy::TextView;
using ::fixy::check::pass;

namespace diag = ::foundation::diag;

// A static_assert can be discharged without the consteval body running
// as written, so the same surface is consumed here from a runtime
// context through volatile sinks the optimizer cannot fold away.
void test_runtime_smoke() {
    constexpr auto msg = diag::row_mismatch_message_v<diag::EffectRowMismatch, &::smoke_fn, int, float, double>;

    // The signedness of char is implementation-defined, so each byte is
    // cast to unsigned before it reaches the exclusive-or.  Each byte of
    // the block goes into the sink, the first and the last too.
    volatile std::size_t sink = msg.size();
    for (char const letter : msg.view()) {
        sink ^= static_cast<unsigned char>(letter);
    }
    (void)sink;

    volatile std::size_t name_sink = diag::type_name<int>.size();
    name_sink ^= diag::type_name<float>.size();
    (void)name_sink;

    volatile std::size_t fn_sink = diag::function_display_name<&::smoke_fn>.size();
    (void)fn_sink;
}

void test_type_name_is_the_printed_name(::fixy::TestRunnerCtx const& test) {
    static_assert(diag::type_name<int> == "int");
    static_assert(diag::type_name<float> == "float");
    static_assert(diag::type_name<void> == "void");

    CRUCIBLE_TEST_ASSERT(test, ends_with(diag::type_name<int>, "int"));
}

void test_function_display_name_non_empty(::fixy::TestRunnerCtx const& test) {
    constexpr auto n1 = diag::function_display_name<&::sample_dispatch>;
    constexpr auto n2 = diag::function_display_name<&::sample_compute>;
    static_assert(n1.size() != 0);
    static_assert(n2.size() != 0);

    CRUCIBLE_TEST_ASSERT(test, n1.size() != 0);
    CRUCIBLE_TEST_ASSERT(test, n2.size() != 0);

    CRUCIBLE_TEST_ASSERT(test, n1 != n2);

    // The extracted name must carry the function's own identifier, not
    // just its pointer-type signature.  The search of the base runs in a
    // constant expression too, so the same search stands in each form.
    static_assert(find(n1, "sample_dispatch").is_some());
    static_assert(find(n2, "sample_compute").is_some());
    ::fixy::report(::fixy::Sink::Err,
                   "\n      [function_display_name<&sample_dispatch>] = {}\n"
                   "      [function_display_name<&sample_compute>]  = {}\n      ",
                   n1, n2);
    CRUCIBLE_TEST_ASSERT(test, find(n1, "sample_dispatch").is_some());
    CRUCIBLE_TEST_ASSERT(test, find(n2, "sample_compute").is_some());
}

// Version 1 is a block of 7 lines. The header derives the line count
// from its literal table, so a line added there fails here. A new line
// is a new format. The JSON records of test/foundation/test_diag.cpp
// contain the version 1 as literal text. A change of the version fails
// that test.
void test_format_version() {
    static_assert(diag::CRUCIBLE_DIAG_FORMAT_LINES == 7,
                  "The block does not have the 7 lines of format version 1. Increase "
                  "CRUCIBLE_DIAG_FORMAT_VERSION and the version in the JSON records of "
                  "test/foundation/test_diag.cpp. Then set the new line count here.");
}

// The count of the newlines of a block.
[[nodiscard]] constexpr std::size_t count_newlines(TextView text) noexcept {
    std::size_t newlines = 0;
    for (char const letter : text) {
        if (letter == '\n') ++newlines;
    }
    return newlines;
}

void test_message_builder_format_shape() {
    // The compile-time checks and the run-time checks read the block
    // through the same text operations of the base, which evaluate in a
    // constant expression too.

    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    static constexpr auto msg =
        diag::build_row_mismatch_message<diag::EffectRowMismatch, &::sample_dispatch, int, float, double>();

    static_assert(starts_with(msg.view(), "[EffectRowMismatch]"));
    static_assert(ends_with(msg.view(), "\n"));
    static_assert(count_newlines(msg.view()) == diag::CRUCIBLE_DIAG_FORMAT_LINES);

    TextView const view = msg.view();
    CRUCIBLE_TEST_ASSERT(test, starts_with(view, "[EffectRowMismatch]"));
    CRUCIBLE_TEST_ASSERT(test, ends_with(view, "\n"));
    CRUCIBLE_TEST_ASSERT(test, find(view, "\n  at ").is_some());
    CRUCIBLE_TEST_ASSERT(test, find(view, "\n  caller row contains: ").is_some());
    CRUCIBLE_TEST_ASSERT(test, find(view, "\n  callee requires:     Subrow<_, ").is_some());
    CRUCIBLE_TEST_ASSERT(test, find(view, "\n  offending atoms:     ").is_some());
    CRUCIBLE_TEST_ASSERT(test, find(view, "\n  remediation: ").is_some());
    CRUCIBLE_TEST_ASSERT(test, find(view, "\n  docs: ").is_some());

    std::size_t const newlines = count_newlines(view);
    CRUCIBLE_TEST_ASSERT(test, newlines == std::size_t{diag::CRUCIBLE_DIAG_FORMAT_LINES});
}

void test_message_builder_per_category(::fixy::TestRunnerCtx const& test) {
    static constexpr auto msg_a =
        diag::build_row_mismatch_message<diag::HotPathViolation, &::sample_dispatch, int, float, double>();
    static constexpr auto msg_b =
        diag::build_row_mismatch_message<diag::DetSafeLeak, &::sample_dispatch, int, float, double>();

    TextView const view_a = msg_a.view();
    TextView const view_b = msg_b.view();

    CRUCIBLE_TEST_ASSERT(test, starts_with(view_a, "[HotPathViolation]"));
    CRUCIBLE_TEST_ASSERT(test, starts_with(view_b, "[DetSafeLeak]"));

    // The category selects the remediation text as well as the tag.
    CRUCIBLE_TEST_ASSERT(test, view_a != view_b);
}

void test_row_mismatch_message_v_caching(::fixy::TestRunnerCtx const& test) {
    constexpr auto& cached1 =
        diag::row_mismatch_message_v<diag::EffectRowMismatch, &::sample_dispatch, int, float, double>;
    constexpr auto& cached2 =
        diag::row_mismatch_message_v<diag::EffectRowMismatch, &::sample_dispatch, int, float, double>;

    // One address for both means the linker collapsed the inline
    // constexpr to a single definition rather than rebuilding the
    // message per use.
    CRUCIBLE_TEST_ASSERT(test, &cached1 == &cached2);

    // The first character of the block is '['.
    static_assert(starts_with(cached1.view(), "["));
    CRUCIBLE_TEST_ASSERT(test, starts_with(cached1.view(), "["));

    CRUCIBLE_TEST_ASSERT(test, starts_with(cached1.view(), "[EffectRowMismatch]"));
}

void test_macro_happy_path() {
    // A true condition must compile silently, so the absence of a
    // diagnostic is the whole claim here.
    CRUCIBLE_DIAG_ROW_MISMATCH_ASSERT(true, EffectRowMismatch, &::sample_dispatch, int, float, double);

    CRUCIBLE_DIAG_ROW_MISMATCH_ASSERT((std::is_same_v<int, int>), HotPathViolation, &::sample_compute, long, short,
                                      char);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_runtime_smoke();
    pass(test, "  test_runtime_smoke: PASSED\n");
    test_type_name_is_the_printed_name(test);
    pass(test, "  test_type_name_is_the_printed_name: PASSED\n");
    test_function_display_name_non_empty(test);
    pass(test, "  test_function_display_name_non_empty: PASSED\n");
    test_format_version();
    pass(test, "  test_format_version: PASSED\n");
    test_message_builder_format_shape();
    pass(test, "  test_message_builder_format_shape: PASSED\n");
    test_message_builder_per_category(test);
    pass(test, "  test_message_builder_per_category: PASSED\n");
    test_row_mismatch_message_v_caching(test);
    pass(test, "  test_row_mismatch_message_v_caching: PASSED\n");
    test_macro_happy_path();
    pass(test, "  test_macro_happy_path: PASSED\n");
    pass(test, "test_row_mismatch: ALL PASSED\n");
    return 0;
}
