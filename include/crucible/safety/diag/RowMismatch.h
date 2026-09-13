#pragma once

// A constraint failure prints an instantiation backtrace and the words
// "constraints not satisfied", which is not enough to act on. The
// builders here compose a block naming the site, the caller's row, the
// row the callee requires, the offending atoms and what to do about it,
// and hand that block to static_assert as its message.
//
// The block is built at compile time and consumed at compile time.
// Nothing survives into the program.
//
// The emitted block is a format, not free text. Editors parse it by
// line, each line holding a fixed role. Appending a line at the end is
// compatible. Reordering lines or changing what a line means is not, and
// changes the version consumers gate on.
//
// The builder is deliberately ignorant of row arithmetic. It takes the
// offending set as an opaque type and stringifies it, so any rejection
// site able to name its own offending information can use this without
// the builder knowing that algebra.

#include <crucible/Platform.h>
#include <crucible/safety/Diagnostic.h>
#include <crucible/safety/diag/Insights.h>
#include <crucible/safety/diag/StableName.h>

#include <array>
#include <cstddef>
#include <meta>
#include <source_location>
#include <string_view>
#include <type_traits>

namespace crucible::safety::diag {

// Consumers gate on this version. The self-test at the end of the file
// ties it to the line count, so adding a line without moving the version
// fails the build rather than reaching a consumer.

inline constexpr std::size_t CRUCIBLE_DIAG_FORMAT_VERSION = 1;

inline constexpr std::size_t CRUCIBLE_DIAG_FORMAT_LINES = 7;

// The same value under a name that reads correctly here. In a cache key
// it is a stable identifier. In a message it is a display name.

template <typename T>
inline constexpr std::string_view type_name = stable_name_of<T>;

// Reflection cannot splice a value: the operator wants an entity, and an
// auto template argument is a value. The name is therefore scraped out
// of the compiler's own rendering of the enclosing function, which does
// spell the argument out. That rendering names the function as the call
// site wrote it, which beats the alternative of printing only the
// function-pointer type.

namespace detail {

template <auto FnPtr>
[[nodiscard]] consteval std::string_view function_name_helper() noexcept {
    // The rendering captured here belongs to this instantiation, so it
    // spells out the template argument.
    return std::source_location::current().function_name();
}

[[nodiscard]] consteval std::string_view extract_pretty_fnptr(std::string_view pretty) noexcept {
    constexpr std::string_view marker{"FnPtr = "};
    std::size_t start = pretty.size();  // sentinel: not found
    for (std::size_t i = 0; i + marker.size() <= pretty.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < marker.size(); ++j) {
            if (pretty[i + j] != marker[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            start = i + marker.size();
            break;
        }
    }
    if (start == pretty.size()) {
        // The rendering no longer matches. Return something the
        // diagnostic can still print.
        return std::string_view{"<unknown function>"};
    }
    // More than one substitution can appear inside the brackets,
    // separated by a semicolon, so the name ends at whichever of the
    // semicolon and the closing bracket comes first. A semicolon cannot
    // occur inside a qualified name, which makes that boundary safe.
    std::size_t end = start;
    for (; end < pretty.size(); ++end) {
        char const c = pretty[end];
        if (c == ']' || c == ';') break;
    }
    return pretty.substr(start, end - start);
}

}  // namespace detail

template <auto FnPtr>
inline constexpr std::string_view function_display_name =
    detail::extract_pretty_fnptr(detail::function_name_helper<FnPtr>());

namespace detail {

template <std::size_t N>
struct char_buffer {
    std::array<char, N> data{};
    std::size_t length = 0;

    constexpr void append(std::string_view s) noexcept {
        for (char c : s) {
            data[length++] = c;
        }
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept { return std::string_view{data.data(), length}; }
};

// The search functions on string_view reach the buffer through data()
// and do pointer arithmetic that does not survive constant evaluation
// there. These helpers index the array instead, which does. They exist
// for the self-test below. A runtime caller uses the view and the
// standard search functions.

template <std::size_t N>
[[nodiscard]] consteval bool buffer_starts_with(char_buffer<N> const& buf, std::string_view prefix) noexcept {
    if (prefix.size() > buf.length) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (buf.data[i] != prefix[i]) return false;
    }
    return true;
}

template <std::size_t N>
[[nodiscard]] consteval bool buffer_ends_with(char_buffer<N> const& buf, std::string_view suffix) noexcept {
    if (suffix.size() > buf.length) return false;
    std::size_t const start = buf.length - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (buf.data[start + i] != suffix[i]) return false;
    }
    return true;
}

// The caller computes the position from the format literals, so there is
// nothing to search for.
template <std::size_t N>
[[nodiscard]] consteval bool buffer_substring_at(char_buffer<N> const& buf, std::size_t pos,
                                                 std::string_view needle) noexcept {
    if (pos + needle.size() > buf.length) return false;
    for (std::size_t i = 0; i < needle.size(); ++i) {
        if (buf.data[pos + i] != needle[i]) return false;
    }
    return true;
}

template <std::size_t N>
[[nodiscard]] consteval std::size_t buffer_count_char(char_buffer<N> const& buf, char needle_char) noexcept {
    std::size_t n = 0;
    for (std::size_t i = 0; i < buf.length; ++i) {
        if (buf.data[i] == needle_char) ++n;
    }
    return n;
}

}  // namespace detail

// The buffer is sized to the exact sum of the literals and the rendered
// names, so the returned type differs per instantiation. That is
// required: only a value of a complete type can be a constant expression
// in a static_assert message.

namespace detail {

inline constexpr std::string_view L1_PREFIX = "[";
inline constexpr std::string_view L1_SUFFIX = "]\n";
inline constexpr std::string_view L2_PREFIX = "  at ";
inline constexpr std::string_view L2_SUFFIX = "\n";
inline constexpr std::string_view L3_PREFIX = "  caller row contains: ";
inline constexpr std::string_view L3_SUFFIX = "\n";
inline constexpr std::string_view L4_PREFIX = "  callee requires:     Subrow<_, ";
inline constexpr std::string_view L4_SUFFIX = ">\n";
inline constexpr std::string_view L5_PREFIX = "  offending atoms:     ";
inline constexpr std::string_view L5_SUFFIX = "\n";
inline constexpr std::string_view L6_PREFIX = "  remediation: ";
inline constexpr std::string_view L6_SUFFIX = "\n";
inline constexpr std::string_view L7_LINE = "  docs: see safety/Diagnostic.h, 28_04_2026_effects.md §7\n";

[[nodiscard]] consteval std::size_t format_total_length(std::string_view category, std::string_view fn_name,
                                                        std::string_view caller_name, std::string_view callee_name,
                                                        std::string_view offending,
                                                        std::string_view remediation) noexcept {
    return L1_PREFIX.size() + category.size() + L1_SUFFIX.size() + L2_PREFIX.size() + fn_name.size() + L2_SUFFIX.size()
         + L3_PREFIX.size() + caller_name.size() + L3_SUFFIX.size() + L4_PREFIX.size() + callee_name.size()
         + L4_SUFFIX.size() + L5_PREFIX.size() + offending.size() + L5_SUFFIX.size() + L6_PREFIX.size()
         + remediation.size() + L6_SUFFIX.size() + L7_LINE.size();
}

}  // namespace detail

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
    requires is_diagnostic_class_v<Tag>
[[nodiscard]] consteval auto build_row_mismatch_message() noexcept {
    constexpr auto category = Tag::name;
    constexpr auto remediation = Tag::remediation;
    constexpr auto fn_name = function_display_name<FnPtr>;
    constexpr auto caller_str = type_name<CallerRow>;
    constexpr auto callee_str = type_name<CalleeRow>;
    constexpr auto offending = type_name<OffendingDiff>;

    constexpr std::size_t N =
        detail::format_total_length(category, fn_name, caller_str, callee_str, offending, remediation);

    detail::char_buffer<N> buf{};
    buf.append(detail::L1_PREFIX);
    buf.append(category);
    buf.append(detail::L1_SUFFIX);
    buf.append(detail::L2_PREFIX);
    buf.append(fn_name);
    buf.append(detail::L2_SUFFIX);
    buf.append(detail::L3_PREFIX);
    buf.append(caller_str);
    buf.append(detail::L3_SUFFIX);
    buf.append(detail::L4_PREFIX);
    buf.append(callee_str);
    buf.append(detail::L4_SUFFIX);
    buf.append(detail::L5_PREFIX);
    buf.append(offending);
    buf.append(detail::L5_SUFFIX);
    buf.append(detail::L6_PREFIX);
    buf.append(remediation);
    buf.append(detail::L6_SUFFIX);
    buf.append(detail::L7_LINE);
    return buf;
}

// Materializing the message as a variable keeps the static_assert's
// message a plain variable reference rather than a call.
//
// Constraining that variable directly with a requires clause reads
// simpler and loses: a misused Tag then fails with the compiler's own
// generic wording, which varies between versions. Routing through the
// helper below produces a message this code controls.

namespace detail {

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff, bool IsTag>
struct row_message_check;

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
struct row_message_check<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff, true> {
    static constexpr auto value = build_row_mismatch_message<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff>();
};

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
struct row_message_check<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff, false> {
    static_assert(is_diagnostic_class_v<Tag>, "crucible::safety::diag [RowMismatchTag_NonTag]: "
                                              "row_mismatch_message_v / CRUCIBLE_ROW_MISMATCH_ASSERT requires "
                                              "Tag to be derived from safety::diag::tag_base.  See "
                                              "safety/Diagnostic.h's catalog for the shipped tag classes; "
                                              "user extensions inherit tag_base + provide constexpr "
                                              "name/description/remediation.");
    // An empty value still exists here, so a downstream read of it does
    // not raise a second error on top of the one above.
    static constexpr char_buffer<1> value{};
};

}  // namespace detail

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
inline constexpr auto& row_mismatch_message_v =
    detail::row_message_check<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff, is_diagnostic_class_v<Tag>>::value;

// The same inputs, plus whatever insight the tag carries. Each insight
// section is skipped when its field is empty, so a tag with no insight
// specialization degrades to the shorter block instead of emitting empty
// headings.

namespace detail {

inline constexpr std::string_view L_SEV_PREFIX = "[";
inline constexpr std::string_view L_SEV_SEP = ": ";
inline constexpr std::string_view L_WHY_HEAD = "  ── why this matters ──\n    ";
inline constexpr std::string_view L_WHY_TAIL = "\n";
inline constexpr std::string_view L_SYM_HEAD = "  ── symptom pattern ──\n    ";
inline constexpr std::string_view L_SYM_TAIL = "\n";
inline constexpr std::string_view L_CORR_HEAD = "  ── correct usage ──\n    ";
inline constexpr std::string_view L_CORR_TAIL = "\n";
inline constexpr std::string_view L_VIOL_HEAD = "  ── violating usage ──\n    ";
inline constexpr std::string_view L_VIOL_TAIL = "\n";

[[nodiscard]] consteval std::size_t deep_format_total_length(std::string_view sev_name, std::string_view category,
                                                             std::string_view fn_name, std::string_view caller_name,
                                                             std::string_view callee_name, std::string_view offending,
                                                             std::string_view why, std::string_view symptom,
                                                             std::string_view correct, std::string_view violating,
                                                             std::string_view remediation) noexcept {
    std::size_t n = L_SEV_PREFIX.size() + sev_name.size() + L_SEV_SEP.size() + category.size() + L1_SUFFIX.size()
                  + L2_PREFIX.size() + fn_name.size() + L2_SUFFIX.size() + L3_PREFIX.size() + caller_name.size()
                  + L3_SUFFIX.size() + L4_PREFIX.size() + callee_name.size() + L4_SUFFIX.size() + L5_PREFIX.size()
                  + offending.size() + L5_SUFFIX.size();

    if (!why.empty()) n += L_WHY_HEAD.size() + why.size() + L_WHY_TAIL.size();
    if (!symptom.empty()) n += L_SYM_HEAD.size() + symptom.size() + L_SYM_TAIL.size();
    if (!correct.empty()) n += L_CORR_HEAD.size() + correct.size() + L_CORR_TAIL.size();
    if (!violating.empty()) n += L_VIOL_HEAD.size() + violating.size() + L_VIOL_TAIL.size();

    n += L6_PREFIX.size() + remediation.size() + L6_SUFFIX.size() + L7_LINE.size();
    return n;
}

}  // namespace detail

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
    requires is_diagnostic_class_v<Tag>
[[nodiscard]] consteval auto build_deep_diagnostic_message() noexcept {
    using P = insight_provider<Tag>;
    constexpr auto sev_name = severity_name(P::severity);
    constexpr auto category = Tag::name;
    constexpr auto remediation = Tag::remediation;
    constexpr auto fn_name = function_display_name<FnPtr>;
    constexpr auto caller_str = type_name<CallerRow>;
    constexpr auto callee_str = type_name<CalleeRow>;
    constexpr auto offending = type_name<OffendingDiff>;
    constexpr auto why = P::why_this_matters;
    constexpr auto symptom = P::symptom_pattern;
    constexpr auto correct = P::correct_example;
    constexpr auto violating = P::violating_example;

    constexpr std::size_t N = detail::deep_format_total_length(
        sev_name, category, fn_name, caller_str, callee_str, offending, why, symptom, correct, violating, remediation);

    detail::char_buffer<N> buf{};
    buf.append(detail::L_SEV_PREFIX);
    buf.append(sev_name);
    buf.append(detail::L_SEV_SEP);
    buf.append(category);
    buf.append(detail::L1_SUFFIX);

    buf.append(detail::L2_PREFIX);
    buf.append(fn_name);
    buf.append(detail::L2_SUFFIX);
    buf.append(detail::L3_PREFIX);
    buf.append(caller_str);
    buf.append(detail::L3_SUFFIX);
    buf.append(detail::L4_PREFIX);
    buf.append(callee_str);
    buf.append(detail::L4_SUFFIX);
    buf.append(detail::L5_PREFIX);
    buf.append(offending);
    buf.append(detail::L5_SUFFIX);

    if constexpr (!P::why_this_matters.empty()) {
        buf.append(detail::L_WHY_HEAD);
        buf.append(why);
        buf.append(detail::L_WHY_TAIL);
    }
    if constexpr (!P::symptom_pattern.empty()) {
        buf.append(detail::L_SYM_HEAD);
        buf.append(symptom);
        buf.append(detail::L_SYM_TAIL);
    }
    if constexpr (!P::correct_example.empty()) {
        buf.append(detail::L_CORR_HEAD);
        buf.append(correct);
        buf.append(detail::L_CORR_TAIL);
    }
    if constexpr (!P::violating_example.empty()) {
        buf.append(detail::L_VIOL_HEAD);
        buf.append(violating);
        buf.append(detail::L_VIOL_TAIL);
    }

    buf.append(detail::L6_PREFIX);
    buf.append(remediation);
    buf.append(detail::L6_SUFFIX);
    buf.append(detail::L7_LINE);
    return buf;
}

namespace detail {

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff, bool IsTag>
struct deep_message_check;

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
struct deep_message_check<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff, true> {
    static constexpr auto value = build_deep_diagnostic_message<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff>();
};

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
struct deep_message_check<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff, false> {
    static_assert(is_diagnostic_class_v<Tag>, "crucible::safety::diag [DeepMismatchTag_NonTag]: "
                                              "row_mismatch_deep_message_v / "
                                              "CRUCIBLE_INSIGHTFUL_ROW_MISMATCH_ASSERT requires Tag to be "
                                              "derived from safety::diag::tag_base.");
    static constexpr char_buffer<1> value{};
};

}  // namespace detail

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
inline constexpr auto& row_mismatch_deep_message_v =
    detail::deep_message_check<Tag, FnPtr, CallerRow, CalleeRow, OffendingDiff, is_diagnostic_class_v<Tag>>::value;

}  // namespace crucible::safety::diag

// Parenthesise a condition containing a comma. The preprocessor splits
// the argument at a template-argument list otherwise.

#define CRUCIBLE_INSIGHTFUL_ROW_MISMATCH_ASSERT(cond, tag, fn, caller, callee, offending) \
    static_assert((cond),                                                                  \
        ::crucible::safety::diag::row_mismatch_deep_message_v<                             \
            ::crucible::safety::diag::tag, fn, caller, callee, offending>                  \
            .view())

// The same rule about parenthesising the condition applies here.

#define CRUCIBLE_ROW_MISMATCH_ASSERT(cond, tag, fn, caller, callee, offending) \
    static_assert(                                                             \
        (cond),                                                                \
        ::crucible::safety::diag::row_mismatch_message_v<::crucible::safety::diag::tag, fn, caller, callee, offending>.view())

namespace crucible::safety::diag {

namespace detail::row_mismatch_self_test {

inline void sample_fn(int, float) noexcept {}

static_assert(type_name<int> == stable_name_of<int>);
static_assert(type_name<float> == stable_name_of<float>);
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
// breaks the format fails at header inclusion instead of reaching a
// consumer that parses it.

constexpr auto sample_msg = build_row_mismatch_message<EffectRowMismatch, &sample_fn, int, float, double>();

static_assert(sample_msg.length > 0, "build_row_mismatch_message returned an empty buffer; format-"
                                     "literal table or input strings collapsed somehow.");
static_assert(sample_msg.length
                  == format_total_length(EffectRowMismatch::name, function_display_name<&sample_fn>, type_name<int>,
                                         type_name<float>, type_name<double>, EffectRowMismatch::remediation),
              "build_row_mismatch_message buffer length diverged from "
              "format_total_length predicted size — fold-loop drift.");

// Newline count: exactly one per line.
static_assert(buffer_count_char(sample_msg, '\n') == CRUCIBLE_DIAG_FORMAT_LINES,
              "Newline count drifted from CRUCIBLE_DIAG_FORMAT_LINES; format-"
              "version-lock-in violated.  Bump CRUCIBLE_DIAG_FORMAT_VERSION when "
              "lines are added.");

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

constexpr auto& cached_msg = row_mismatch_message_v<HotPathViolation, &sample_fn, int, float, double>;

constexpr auto& cached_msg2 = row_mismatch_message_v<DetSafeLeak, &sample_fn, int, float, double>;

// Caching: same template instantiation → same address (linker
// collapses inline constexpr to one definition).
constexpr auto& cached_msg_again = row_mismatch_message_v<HotPathViolation, &sample_fn, int, float, double>;
static_assert(&cached_msg == &cached_msg_again);

static_assert(buffer_substring_at(cached_msg, L1_PREFIX.size(), HotPathViolation::name));
static_assert(buffer_substring_at(cached_msg2, L1_PREFIX.size(), DetSafeLeak::name));

// The version and the line count move together. These two catch the
// change that moves one without the other.

static_assert(CRUCIBLE_DIAG_FORMAT_VERSION == 1, "CRUCIBLE_DIAG_FORMAT_VERSION drifted from 1 — verify "
                                                 "CRUCIBLE_DIAG_FORMAT_LINES + literal-table sizes were updated "
                                                 "in lockstep, AND that downstream consumers were notified.");

static_assert(CRUCIBLE_DIAG_FORMAT_LINES == 7, "CRUCIBLE_DIAG_FORMAT_LINES drifted from the 7 lines of version 1 — "
                                               "verify CRUCIBLE_DIAG_FORMAT_VERSION was bumped accordingly.");

static_assert(CRUCIBLE_DIAG_FORMAT_VERSION == 1);

}  // namespace detail::row_mismatch_self_test

namespace detail::row_mismatch_smoke {
inline void smoke_fn(int) noexcept {}
}  // namespace detail::row_mismatch_smoke

// A static_assert can be discharged without the consteval body running
// as written, so the same surface is consumed here from a runtime
// context through volatile sinks the optimizer cannot fold away.

inline void runtime_smoke_test_row_mismatch() noexcept {
    constexpr auto msg =
        row_mismatch_message_v<EffectRowMismatch, &detail::row_mismatch_smoke::smoke_fn, int, float, double>;

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

    volatile std::size_t name_sink = type_name<int>.size();
    name_sink ^= type_name<float>.size();
    (void)name_sink;

    volatile std::size_t fn_sink = function_display_name<&detail::row_mismatch_smoke::smoke_fn>.size();
    (void)fn_sink;
}

}  // namespace crucible::safety::diag
