#pragma once

// The free operations of the Region family: compare, search and scan a View
// or a TextView, and read the bytes of a plain object.
//
//   equal(a, b)                two Views of one element type, or two texts:
//                              the same count and the same elements in the
//                              same order
//   order(a, b)                two Views of one element type: the
//                              lexicographic order of their elements
//   find(view, value)          the position of the first element equal to
//                              value, or no value
//   find(text, c), find(text, needle)
//                              the position of the first character c, or of
//                              the first run of the characters of needle
//   find_if(view, predicate)   the position of the first element that the
//                              predicate accepts, or no value
//   starts_with, ends_with     a text that begins or ends with a second text
//   find_lower_bound_by_key(view, key, probe)
//                              the first position whose key is not below the
//                              probe, in a View that its key sorts
//   bytes(object)              the bytes of a plain object, as a View of its
//                              size
//   is_window_of(part, whole)  a text whose characters lie inside the
//                              characters of a second text.  The two texts
//                              come in their roles (WindowPartText and
//                              WindowWholeText), so a call cannot swap them
//
// Each operation reads only the elements of its Views, so no operation
// reads past a View.  A position is an index below the count of its View.
// Each operation is total: a search that finds nothing gives an empty
// Option, and no operation ends the process.  A predicate and a key do not
// throw, and a scan calls each one time for each element, in order.
//
// A test of each element or of one element, and a count with a test, are
// terminals of a walk (foundation/core/Walk.h):
// view.walk().any_of(holds), view.walk().all_of(holds) and
// view.walk().filter(keep).count().

#include <foundation/core/Choice.h>
#include <foundation/core/Equatable.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Side.h>
#include <foundation/core/Text.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::core {

namespace detail {

// An element whose equal is the compare of its bytes: an integer or an
// enumeration with no padding bit, or a float or a double, which compares by
// its bits.  equal compares a run of such elements with one memcmp.
template <class T>
concept BytewiseEqualElement =
    ((std::is_integral_v<T> || std::is_enum_v<T>) && std::has_unique_object_representations_v<T>) || Float<T>;

// An element whose order is the order of its bytes: a byte with no sign.
// order compares a run of such elements with one memcmp.
template <class T>
concept BytewiseOrderedElement = std::same_as<T, unsigned char> || std::same_as<T, std::byte>;

// A predicate of a scan: it takes an element as a const reference, gives a
// bool, does not throw, and holds no linear value by value
// (HoldsNoLinearValue of foundation/Linear.h).  The door calls it many times
// and ends it, and a call is no consume.
template <class Predicate, class T>
concept ElementPredicate =
    std::is_nothrow_invocable_v<Predicate&, T const&> && std::same_as<std::invoke_result_t<Predicate&, T const&>, bool>
    && ::foundation::linear::HoldsNoLinearValue<Predicate>;

// A predicate or a key function that holds a linear value by value: the
// deleted twin of find_if and of find_lower_bound_by_key binds to it.
template <class Function, class T>
concept LinearElementFunction =
    (!::foundation::linear::HoldsNoLinearValue<Function>) && std::is_invocable_v<Function&, T const&>;

// A key of an ordered search: an integer or an enumeration, so the order of
// the keys is total.  A floating key is refused, because a NaN is in no
// order.
template <class K>
concept TotalKey = (std::is_integral_v<K> && !std::same_as<K, bool>) || std::is_enum_v<K>;

// A key function: it takes an element as a const reference, gives a total
// key, does not throw, and holds no linear value by value, for the reason of
// ElementPredicate.
template <class Key, class T>
concept KeyOf = std::is_nothrow_invocable_v<Key&, T const&> && TotalKey<std::invoke_result_t<Key&, T const&>>
             && ::foundation::linear::HoldsNoLinearValue<Key>;

}  // namespace detail

// True when the two Views hold the same count of elements, and each
// element equals the element at the same position of the other View, by
// equal of the Protocol family (foundation/core/Equatable.h).  A float
// compares by its bits.  An integer, an enumeration, a float and a double
// take one memcmp, which is the compare of the raw form.  Complexity: O(n).
template <class T, std::size_t LeftExtent, class U, std::size_t RightExtent>
    requires std::same_as<std::remove_const_t<T>, std::remove_const_t<U>> && Equatable<std::remove_const_t<T>>
[[nodiscard]] constexpr bool equal(View<T, LeftExtent> left, View<U, RightExtent> right) noexcept {
    using Element = std::remove_const_t<T>;
    std::size_t const count = left.size();
    if (count != right.size()) return false;
    if (count == 0) return true;
    Element const* const first = first_of_(detail::BaseLayerRead{}, left);
    Element const* const second = first_of_(detail::BaseLayerRead{}, right);
    if constexpr (detail::BytewiseEqualElement<Element>) {
        if !consteval {
            return __builtin_memcmp(first, second, count * sizeof(Element)) == 0;
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!::foundation::core::equal(first[index], second[index])) return false;
    }
    return true;
}

// The lexicographic order of the two Views, by order of the Protocol
// family: the order of the first pair of elements that differ, or, when one
// View is a prefix of the other, the order of the counts.  A float orders by
// the totalOrder of IEEE 754.  A run of unsigned bytes takes one memcmp.
// Complexity: O(n) for the shorter count n.
template <class T, std::size_t LeftExtent, class U, std::size_t RightExtent>
    requires std::same_as<std::remove_const_t<T>, std::remove_const_t<U>> && Orderable<std::remove_const_t<T>>
[[nodiscard]] constexpr std::strong_ordering order(View<T, LeftExtent> left, View<U, RightExtent> right) noexcept {
    using Element = std::remove_const_t<T>;
    std::size_t const left_count = left.size();
    std::size_t const right_count = right.size();
    std::size_t const common = left_count < right_count ? left_count : right_count;
    Element const* const first = first_of_(detail::BaseLayerRead{}, left);
    Element const* const second = first_of_(detail::BaseLayerRead{}, right);
    if constexpr (detail::BytewiseOrderedElement<Element>) {
        if !consteval {
            if (common != 0) {
                int const bytes_order = __builtin_memcmp(first, second, common);
                if (bytes_order != 0) return bytes_order <=> 0;
            }
            return left_count <=> right_count;
        }
    }
    for (std::size_t index = 0; index < common; ++index) {
        std::strong_ordering const step = ::foundation::core::order(first[index], second[index]);
        if (std::is_neq(step)) return step;
    }
    return left_count <=> right_count;
}

// True when the two texts hold the same characters in the same order.
[[nodiscard]] constexpr bool equal(TextView left, TextView right) noexcept { return left == right; }

// The position of the first element equal to value, or no value.
// Complexity: O(n).
template <class T, std::size_t Extent>
    requires Equatable<std::remove_const_t<T>>
[[nodiscard]] constexpr Option<std::size_t> find(View<T, Extent> view,
                                                 std::type_identity_t<std::remove_const_t<T>> const& value) noexcept {
    std::remove_const_t<T> const* const first = first_of_(detail::BaseLayerRead{}, view);
    for (std::size_t index = 0; index < view.size(); ++index) {
        if (::foundation::core::equal(first[index], value)) return Option<std::size_t>::some(index);
    }
    return none;
}

// The position of the first character equal to character, or no value.
// Complexity: O(n).
[[nodiscard]] constexpr Option<std::size_t> find(TextView text, char character) noexcept {
    char const* const first = detail::text_first_(text);
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (first[index] == character) return Option<std::size_t>::some(index);
    }
    return none;
}

// The position of the first run of the characters of needle in text, or no
// value.  An empty needle is at position 0.  Complexity: O(n * m) for a
// text of n characters and a needle of m characters.
[[nodiscard]] constexpr Option<std::size_t> find(TextView text, TextView needle) noexcept {
    std::size_t const size = text.size();
    std::size_t const needle_size = needle.size();
    if (needle_size > size) return none;
    char const* const first = detail::text_first_(text);
    char const* const needle_first = detail::text_first_(needle);
    for (std::size_t start = 0; start <= size - needle_size; ++start) {
        std::size_t matched = 0;
        while (matched < needle_size && first[start + matched] == needle_first[matched]) {
            ++matched;
        }
        if (matched == needle_size) return Option<std::size_t>::some(start);
    }
    return none;
}

// The position of the first element that predicate accepts, or no value.
// Complexity: O(n) calls of the predicate.
template <class T, std::size_t Extent, class Predicate>
    requires detail::ElementPredicate<Predicate, std::remove_const_t<T>>
[[nodiscard]] constexpr Option<std::size_t> find_if(View<T, Extent> view, Predicate predicate) noexcept {
    std::remove_const_t<T> const* const first = first_of_(detail::BaseLayerRead{}, view);
    for (std::size_t index = 0; index < view.size(); ++index) {
        if (predicate(first[index])) return Option<std::size_t>::some(index);
    }
    return none;
}

// A predicate that holds a linear value by value binds to this overload, so
// the refusal gives its reason (LinearElementFunction).
template <class T, std::size_t Extent, class Predicate>
    requires detail::LinearElementFunction<Predicate, std::remove_const_t<T>>
void find_if(View<T, Extent> view, Predicate predicate) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// True when text begins with the characters of prefix.  An empty prefix
// gives true.  Complexity: O(m) for a prefix of m characters.
[[nodiscard]] constexpr bool starts_with(TextView text, TextView prefix) noexcept {
    std::size_t const prefix_size = prefix.size();
    if (prefix_size > text.size()) return false;
    char const* const first = detail::text_first_(text);
    char const* const prefix_first = detail::text_first_(prefix);
    for (std::size_t index = 0; index < prefix_size; ++index) {
        if (first[index] != prefix_first[index]) return false;
    }
    return true;
}

// True when text ends with the characters of suffix.  An empty suffix gives
// true.  Complexity: O(m) for a suffix of m characters.
[[nodiscard]] constexpr bool ends_with(TextView text, TextView suffix) noexcept {
    std::size_t const size = text.size();
    std::size_t const suffix_size = suffix.size();
    if (suffix_size > size) return false;
    char const* const first = detail::text_first_(text) + (size - suffix_size);
    char const* const suffix_first = detail::text_first_(suffix);
    for (std::size_t index = 0; index < suffix_size; ++index) {
        if (first[index] != suffix_first[index]) return false;
    }
    return true;
}

// The roles of the two texts of is_window_of (foundation/core/Side.h).
namespace side {

// The text that can lie inside the other text.
struct WindowPart final {};

// The text that can hold the other text.
struct WindowWhole final {};

}  // namespace side

// The two texts of is_window_of in their roles.
using WindowPartText = Side<side::WindowPart, TextView>;
using WindowWholeText = Side<side::WindowWhole, TextView>;

// True when each character of part lies inside whole: the first address of
// part is at or after the first address of whole, and the end of part is at
// or before the end of whole.  The query reads the addresses and no
// character, so a text of a different object with the same characters is no
// window.  An empty part is a window when its address lies from the first
// address of whole to the end of whole, so an empty part at the end of whole
// is a window.  The empty text with no address, such as a default TextView,
// is a window only of a text with no address.
//
// The query is offset_of of foundation/core/Region.h over the characters of
// the two texts.  At run time, offset_of compares the two address ranges as
// integers.  A relational compare of two pointers into different objects
// has no specified result in C++, but a compare of two integers has one.
// Thus a text of a different object gives false, and the compare has no
// undefined behavior.  In a constant evaluation, an empty part of a different
// object can meet a compare with the address one past the end of whole, and
// such a call does not compile.  Complexity: O(1), two compares, at run
// time, and O(whole.size()) in a constant evaluation.
[[nodiscard]] constexpr bool is_window_of(WindowPartText const& part, WindowWholeText const& whole) noexcept {
    TextView const part_text = part.value();
    TextView const whole_text = whole.value();
    View<char const> const part_view = detail::view_of_text_(part_text);
    View<char const> const whole_view = detail::view_of_text_(whole_text);
    return offset_of(whole_view, part_view).is_some();
}

// The first position whose key is not below probe.  The View must be in
// the order of its keys, from the lowest key.  The result is a position
// from 0 to the count of the View, so a View in a different order gives a
// position of no meaning, and no read past the View.  The key function runs
// one time for each step.  Complexity: O(log n).
template <class T, std::size_t Extent, class Key>
    requires detail::KeyOf<Key, std::remove_const_t<T>>
[[nodiscard]] constexpr std::size_t
find_lower_bound_by_key(View<T, Extent> view, Key key,
                        std::invoke_result_t<Key&, std::remove_const_t<T> const&> probe) noexcept {
    std::remove_const_t<T> const* const first = first_of_(detail::BaseLayerRead{}, view);
    std::size_t low = 0;
    std::size_t high = view.size();
    while (low < high) {
        std::size_t const middle = low + (high - low) / 2;
        if (key(first[middle]) < probe) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

// A key function that holds a linear value by value binds to this overload,
// so the refusal gives its reason (LinearElementFunction).
template <class T, std::size_t Extent, class Key, class Probe>
    requires detail::LinearElementFunction<Key, std::remove_const_t<T>>
void find_lower_bound_by_key(View<T, Extent> view, Key key, Probe probe) = delete(CRUCIBLE_LINEAR_BODY_REFUSAL);

// An object whose bytes are its whole value: each byte is a value byte, and
// the walk of Scalar accepts its type, so it holds no pointer, no
// reference, no union, no bit-field, no empty class and no sealed class.
// Its bytes then reach a hash or a wire with no padding and no address.
template <class T>
concept ByteImageable =
    std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T> && detail::is_plain_at(^^T, 0);

// The bytes of object, as a View of its size.  The View borrows object, so
// the bytes of a temporary are refused: the View would dangle at the end of
// the full expression.  The cast of the address is no constant expression,
// so the operation is not constexpr.
template <ByteImageable T>
[[nodiscard]] View<std::byte const, sizeof(T)> bytes(T const& object) noexcept {
    return detail::storage_of_object_(object);
}

template <ByteImageable T>
void bytes(T const&&) = delete("the bytes of a temporary object dangle at the end of the full expression");

}  // namespace foundation::core
