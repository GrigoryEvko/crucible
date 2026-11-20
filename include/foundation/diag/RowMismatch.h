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
//
// The runtime test is test/foundation/test_row_mismatch.cpp.

#include <foundation/Platform.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Text.h>
#include <foundation/core/Verbs.h>
#include <foundation/diag/Tag.h>

#include <cstddef>
#include <meta>
#include <source_location>
#include <type_traits>

namespace foundation::diag {

// Consumers gate on this version. A version is a choice, not a count: a
// change to the block or to the JSON record that a consumer can notice
// raises it. The JSON record carries the version, and
// test/foundation/test_diag.cpp compares that record byte for byte.
// test/foundation/test_row_mismatch.cpp pins the line count that goes
// with the version, so a line added without a new version fails there.

inline constexpr std::size_t CRUCIBLE_DIAG_FORMAT_VERSION = 1;

// The printed name of a type, for a message.  A message needs no stable
// identity, so the name does not refuse a type that has none, and no key
// reads it.  The text is in static storage, with a zero after its last
// character.

template <typename T>
inline constexpr ::foundation::core::TextView type_name =
    ::foundation::core::detail::static_text_<^^T>(std::meta::display_string_of(^^T));

// Reflection cannot splice a value: the operator wants an entity, and an
// auto template argument is a value. The name is therefore scraped out
// of the compiler's own rendering of the enclosing function, which does
// spell the argument out. That rendering names the function as the call
// site wrote it, which beats the alternative of printing only the
// function-pointer type.

namespace detail {

template <auto FnPtr>
[[nodiscard]] consteval ::foundation::core::TextView function_name_helper() noexcept {
    // The rendering captured here belongs to this instantiation, so it
    // spells out the template argument.  The rendering is a static string
    // with a zero after its last character.
    return ::foundation::core::mint_static_text(std::source_location::current().function_name());
}

[[nodiscard]] consteval ::foundation::core::TextView
extract_pretty_fnptr(::foundation::core::TextView pretty) noexcept {
    using ::foundation::core::TextView;
    constexpr TextView marker{"FnPtr = "};
    // The rendering no longer matches when it holds no marker. The
    // diagnostic then prints a text that it can still show.
    constexpr TextView unknown{"<unknown function>"};
    // A match starts before the last character, so the size of the
    // rendering marks no match.
    std::size_t const found = ::foundation::core::find(pretty, marker).value_or(pretty.size());
    if (found == pretty.size()) return unknown;
    std::size_t const start = found + marker.size();
    TextView const rest = pretty.window(start, pretty.size() - start).value_or(TextView{});
    // More than one substitution can appear inside the brackets,
    // separated by a semicolon, so the name ends at whichever of the
    // semicolon and the closing bracket comes first. A semicolon cannot
    // occur inside a qualified name, which makes that boundary safe.
    std::size_t length = 0;
    for (char const letter : rest) {
        if (letter == ']' || letter == ';') break;
        ++length;
    }
    return rest.window(0, length).value_or(unknown);
}

}  // namespace detail

template <auto FnPtr>
inline constexpr ::foundation::core::TextView function_display_name =
    detail::extract_pretty_fnptr(detail::function_name_helper<FnPtr>());

namespace detail {

// Has no constant definition.  A call to it stops the constant evaluation,
// and the diagnostic names it.
void row_mismatch_text_overflows_its_buffer() noexcept;

// The text of one block.  Capacity is the exact length of the block, so the
// type differs for each block: only a value of a complete type can be the
// message of a static_assert.  data() and size() give the text, which
// static_assert prints as its message, and view() gives it as a TextView.
// The byte after the last character stays zero, so a zero follows the text.
template <std::size_t Capacity>
class char_buffer {
    char text_[Capacity + 1]{};
    std::size_t size_ = 0;

public:
    // Puts each character of text after the last character.  A text that
    // does not fit stops the constant evaluation.  Complexity: one copy for
    // each character.
    consteval void append(::foundation::core::TextView text) noexcept {
        for (char const letter : text) {
            if (size_ == Capacity) row_mismatch_text_overflows_its_buffer();
            text_[size_] = letter;
            ++size_;
        }
    }

    [[nodiscard]] constexpr char const* data() const noexcept { return text_; }
    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }

    // The text.  The view borrows this buffer, so a view of a temporary
    // buffer is refused: it would dangle at the end of the full expression.
    [[nodiscard]] constexpr ::foundation::core::TextView view() const& noexcept {
        return ::foundation::core::detail::c_text_over_(text_, size_).view();
    }
    ::foundation::core::TextView view() const&& = delete("a view of a temporary char_buffer points into an object "
                                                         "that the full expression ends");
};

// The TextView of a text field of a tag.  The field holds its characters
// and their count, and the view takes the two.
template <class Field>
[[nodiscard]] consteval ::foundation::core::TextView tag_field_text_(Field const& field) noexcept {
    return ::foundation::core::detail::text_over_(field.data(), field.size());
}

}  // namespace detail

// The buffer is sized to the exact sum of the literals and the rendered
// names, so the returned type differs per instantiation. That is
// required: only a value of a complete type can be a constant expression
// in a static_assert message.

namespace detail {

inline constexpr ::foundation::core::TextView L1_PREFIX = "[";
inline constexpr ::foundation::core::TextView L1_SUFFIX = "]\n";
inline constexpr ::foundation::core::TextView L2_PREFIX = "  at ";
inline constexpr ::foundation::core::TextView L2_SUFFIX = "\n";
inline constexpr ::foundation::core::TextView L3_PREFIX = "  caller row contains: ";
inline constexpr ::foundation::core::TextView L3_SUFFIX = "\n";
inline constexpr ::foundation::core::TextView L4_PREFIX = "  callee requires:     Subrow<_, ";
inline constexpr ::foundation::core::TextView L4_SUFFIX = ">\n";
inline constexpr ::foundation::core::TextView L5_PREFIX = "  offending atoms:     ";
inline constexpr ::foundation::core::TextView L5_SUFFIX = "\n";
inline constexpr ::foundation::core::TextView L6_PREFIX = "  remediation: ";
inline constexpr ::foundation::core::TextView L6_SUFFIX = "\n";
inline constexpr ::foundation::core::TextView L7_LINE =
    "  docs: see foundation/diag/Catalog.h, 28_04_2026_effects.md §7\n";

// Every literal of the block, in the order that the builder appends them.
// The length and the line count of the block come from this table, and
// the check file of this header holds the builder to both.
inline constexpr ::foundation::core::TextView row_mismatch_literals[] = {
    L1_PREFIX, L1_SUFFIX, L2_PREFIX, L2_SUFFIX, L3_PREFIX, L3_SUFFIX, L4_PREFIX,
    L4_SUFFIX, L5_PREFIX, L5_SUFFIX, L6_PREFIX, L6_SUFFIX, L7_LINE};

[[nodiscard]] consteval std::size_t format_newline_count() noexcept {
    std::size_t count = 0;
    for (::foundation::core::TextView const literal : row_mismatch_literals) {
        for (char const c : literal) {
            if (c == '\n') ++count;
        }
    }
    return count;
}

// The length of a block with the six texts.  The length is a sum, so the
// texts can change places with no change of the result.  Complexity: one
// add for each text and for each literal.
[[nodiscard]] consteval std::size_t
format_total_length(::foundation::core::TextView category, ::foundation::core::TextView fn_name,
                    ::foundation::core::TextView caller_name, ::foundation::core::TextView callee_name,
                    ::foundation::core::TextView offending, ::foundation::core::TextView remediation) noexcept {
    std::size_t length = category.size() + fn_name.size() + caller_name.size() + callee_name.size() + offending.size()
                       + remediation.size();
    for (::foundation::core::TextView const literal : row_mismatch_literals) {
        length += literal.size();
    }
    return length;
}

}  // namespace detail

// A newline ends each line of the block, and only the literals hold one.
inline constexpr std::size_t CRUCIBLE_DIAG_FORMAT_LINES = detail::format_newline_count();

template <typename Tag, auto FnPtr, typename CallerRow, typename CalleeRow, typename OffendingDiff>
    requires is_diagnostic_class_v<Tag>
[[nodiscard]] consteval auto build_row_mismatch_message() noexcept {
    constexpr auto category = detail::tag_field_text_(Tag::name);
    constexpr auto remediation = detail::tag_field_text_(Tag::remediation);
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
    static_assert(is_diagnostic_class_v<Tag>, "foundation::diag [RowMismatchTag_NonTag]: "
                                              "row_mismatch_message_v / CRUCIBLE_DIAG_ROW_MISMATCH_ASSERT requires "
                                              "Tag to be derived from foundation::diag::tag_base.  See "
                                              "foundation/diag/Catalog.h's catalog for the shipped tag classes; "
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

}  // namespace foundation::diag

// Parenthesise a condition containing a comma. The preprocessor splits
// the argument at a template-argument list otherwise.

#define CRUCIBLE_DIAG_ROW_MISMATCH_ASSERT(cond, tag, fn, caller, callee, offending) \
    static_assert((cond),                                                           \
                  ::foundation::diag::row_mismatch_message_v<::foundation::diag::tag, fn, caller, callee, offending>)
