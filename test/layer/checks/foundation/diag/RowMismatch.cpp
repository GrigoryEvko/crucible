// The compile-time checks of foundation/diag/RowMismatch.h.

#include <foundation/diag/RowMismatch.h>

namespace foundation::diag {

namespace detail::row_mismatch_self_test {

inline void sample_fn(int, float) noexcept {}

static_assert(type_name<int> == ::foundation::reflect::stable_name_of<int>);
static_assert(type_name<float> == ::foundation::reflect::stable_name_of<float>);
static_assert(!type_name<int>.empty());
static_assert(type_name<int>.ends_with("int"));

static_assert(!function_display_name<&sample_fn>.empty());

constexpr auto fmt_len =
    format_total_length(std::string_view{"Cat"}, std::string_view{"fn(...)"}, std::string_view{"Caller"},
                        std::string_view{"Callee"}, std::string_view{"Offend"}, std::string_view{"Fix"});

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

static_assert(sample_msg.length > 0, "build_row_mismatch_message returned an empty buffer; format-"
                                     "literal table or input strings collapsed somehow.");
static_assert(sample_msg.length
                  == format_total_length(EffectRowMismatch::name, function_display_name<&sample_fn>, type_name<int>,
                                         type_name<float>, type_name<double>, EffectRowMismatch::remediation),
              "build_row_mismatch_message buffer length diverged from "
              "format_total_length predicted size — fold-loop drift.");

// The builder ends each line with exactly one newline, and the inputs
// add none.
static_assert(buffer_count_char(sample_msg, '\n') == CRUCIBLE_DIAG_FORMAT_LINES,
              "The block has a different number of lines than the literal table "
              "row_mismatch_literals holds. The builder and the table disagree.");

// Line 1 starts with "[" + Category::name + "]\n" at known position.
static_assert(buffer_starts_with(sample_msg, "["));
static_assert(buffer_substring_at(sample_msg, std::size_t{0}, L1_PREFIX));
static_assert(buffer_substring_at(sample_msg, L1_PREFIX.size(), EffectRowMismatch::name));
static_assert(buffer_substring_at(sample_msg, L1_PREFIX.size() + EffectRowMismatch::name.size(), L1_SUFFIX));

// Line 2 starts with "  at " at the known offset.
constexpr std::size_t line2_off = L1_PREFIX.size() + EffectRowMismatch::name.size() + L1_SUFFIX.size();
static_assert(buffer_substring_at(sample_msg, line2_off, L2_PREFIX));

// Line 3 starts with "  caller row contains: " at the known offset.
constexpr std::size_t line3_off =
    line2_off + L2_PREFIX.size() + function_display_name<&sample_fn>.size() + L2_SUFFIX.size();
static_assert(buffer_substring_at(sample_msg, line3_off, L3_PREFIX));

// Buffer ends with "\n" — Line 7 suffix.
static_assert(buffer_ends_with(sample_msg, "\n"));

// Buffer ends with the docs line, fully.
static_assert(buffer_ends_with(sample_msg, L7_LINE));

// A reference at namespace scope is not an object, so constexpr does
// not give it internal linkage the way it does the constants above.
// Each reference is inline, so the program holds one definition of it.
inline constexpr auto& cached_msg = row_mismatch_message_v<HotPathViolation, &sample_fn, int, float, double>;

inline constexpr auto& cached_msg2 = row_mismatch_message_v<DetSafeLeak, &sample_fn, int, float, double>;

// Caching: same template instantiation → same address (linker
// collapses inline constexpr to one definition).
inline constexpr auto& cached_msg_again = row_mismatch_message_v<HotPathViolation, &sample_fn, int, float, double>;
static_assert(&cached_msg == &cached_msg_again);

static_assert(buffer_substring_at(cached_msg, L1_PREFIX.size(), HotPathViolation::name));
static_assert(buffer_substring_at(cached_msg2, L1_PREFIX.size(), DetSafeLeak::name));

}  // namespace detail::row_mismatch_self_test

}  // namespace foundation::diag
