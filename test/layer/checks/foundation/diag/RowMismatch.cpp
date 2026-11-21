// The compile-time checks of foundation/diag/RowMismatch.h.

#include <foundation/diag/RowMismatch.h>

#include <foundation/core/Algorithms.h>
#include <foundation/core/Text.h>
#include <foundation/diag/tags/Rows.h>
#include <foundation/diag/tags/Tiers.h>

#include <cstddef>

namespace foundation::diag {

namespace detail::row_mismatch_self_test {

using ::foundation::core::ends_with;
using ::foundation::core::starts_with;
using ::foundation::core::TextView;

inline void sample_fn(int, float) noexcept {}

static_assert(type_name<int> == "int");
static_assert(type_name<float> == "float");
static_assert(type_name<int>.size() != 0);
static_assert(ends_with(type_name<int>, "int"));

static_assert(function_display_name<&sample_fn>.size() != 0);

constexpr auto fmt_len = format_total_length(TextView{"Cat"}, TextView{"fn(...)"}, TextView{"Caller"},
                                             TextView{"Callee"}, TextView{"Offend"}, TextView{"Fix"});

constexpr std::size_t expected_len = L1_PREFIX.size() + 3 + L1_SUFFIX.size() + L2_PREFIX.size() + 7 + L2_SUFFIX.size()
                                   + L3_PREFIX.size() + 6 + L3_SUFFIX.size() + L4_PREFIX.size() + 6 + L4_SUFFIX.size()
                                   + L5_PREFIX.size() + 6 + L5_SUFFIX.size() + L6_PREFIX.size() + 3 + L6_SUFFIX.size()
                                   + L7_LINE.size();

static_assert(fmt_len == expected_len, "format_total_length must equal the sum of literal sizes plus "
                                       "input string sizes; drift indicates a literal-table edit "
                                       "without a corresponding format_total_length update.");

// These pin the shape of the emitted block line by line, so an edit that
// breaks the format fails in the build instead of reaching a consumer
// that parses it.

constexpr auto sample_msg = build_row_mismatch_message<EffectRowMismatch, &sample_fn, int, float, double>();
constexpr TextView sample_text = sample_msg.view();

// The names of the tags, for the offsets below.
constexpr TextView effect_row_name = EffectRowMismatch::name;
constexpr TextView hot_path_name = HotPathViolation::name;
constexpr TextView det_safe_name = DetSafeLeak::name;

// The characters of the sample block from Offset to its end, or the empty
// text when Offset is past the end.
template <std::size_t Offset>
constexpr TextView text_from = sample_text.window(Offset, sample_text.size() - Offset).value_or(TextView{});

static_assert(sample_msg.size() > 0, "build_row_mismatch_message returned an empty buffer; format-"
                                     "literal table or input strings collapsed somehow.");
static_assert(sample_msg.size()
                  == format_total_length(effect_row_name, function_display_name<&sample_fn>, type_name<int>,
                                         type_name<float>, type_name<double>, EffectRowMismatch::remediation),
              "build_row_mismatch_message buffer length diverged from "
              "format_total_length predicted size — fold-loop drift.");

// The count of the newlines of the block.
constexpr std::size_t sample_newlines = [] consteval {
    std::size_t count = 0;
    for (char const letter : sample_text) {
        if (letter == '\n') ++count;
    }
    return count;
}();

// The builder ends each line with exactly one newline, and the inputs
// add none.
static_assert(sample_newlines == CRUCIBLE_DIAG_FORMAT_LINES,
              "The block has a different number of lines than the literal table "
              "row_mismatch_literals holds. The builder and the table disagree.");

// Line 1 starts with "[" + Category::name + "]\n" at known position.
static_assert(starts_with(sample_text, "["));
static_assert(starts_with(text_from<0>, L1_PREFIX));
static_assert(starts_with(text_from<L1_PREFIX.size()>, effect_row_name));
static_assert(starts_with(text_from<L1_PREFIX.size() + effect_row_name.size()>, L1_SUFFIX));

// Line 2 starts with "  at " at the known offset.
constexpr std::size_t line2_off = L1_PREFIX.size() + effect_row_name.size() + L1_SUFFIX.size();
static_assert(starts_with(text_from<line2_off>, L2_PREFIX));

// Line 3 starts with "  caller row contains: " at the known offset.
constexpr std::size_t line3_off =
    line2_off + L2_PREFIX.size() + function_display_name<&sample_fn>.size() + L2_SUFFIX.size();
static_assert(starts_with(text_from<line3_off>, L3_PREFIX));

// Buffer ends with "\n" — Line 7 suffix.
static_assert(ends_with(sample_text, "\n"));

// Buffer ends with the docs line, fully.
static_assert(ends_with(sample_text, L7_LINE));

// A reference at namespace scope is not an object, so constexpr does
// not give it internal linkage the way it does the constants above.
// Each reference is inline, so the program holds one definition of it.
inline constexpr auto& cached_msg = row_mismatch_message_v<HotPathViolation, &sample_fn, int, float, double>;

inline constexpr auto& cached_msg2 = row_mismatch_message_v<DetSafeLeak, &sample_fn, int, float, double>;

// Caching: same template instantiation → same address (linker
// collapses inline constexpr to one definition).
inline constexpr auto& cached_msg_again = row_mismatch_message_v<HotPathViolation, &sample_fn, int, float, double>;
static_assert(&cached_msg == &cached_msg_again);

static_assert(starts_with(cached_msg.view().window(L1_PREFIX.size(), hot_path_name.size()).value_or(TextView{}),
                          hot_path_name));
static_assert(starts_with(cached_msg2.view().window(L1_PREFIX.size(), det_safe_name.size()).value_or(TextView{}),
                          det_safe_name));

}  // namespace detail::row_mismatch_self_test

}  // namespace foundation::diag
