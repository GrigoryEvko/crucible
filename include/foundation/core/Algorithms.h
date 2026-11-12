#pragma once

// The free operations of the Region family: compare, search and scan a View
// or a TextView, and read the bytes of a plain object.
//
//   equal(a, b)                two Views of one element type, or two texts:
//                              the same count and the same elements in the
//                              same order
//   find(view, value)          the position of the first element equal to
//                              value, or no value
//   find(text, c), find(text, needle)
//                              the position of the first character c, or of
//                              the first run of the characters of needle
//   find_if(view, predicate)   the position of the first element that the
//                              predicate accepts, or no value
//   all_of, any_of, count_if   a scan of each element with a predicate
//   starts_with, ends_with     a text that begins or ends with a second text
//   lower_bound_by_key(view, key, probe)
//                              the first position whose key is not below the
//                              probe, in a View that its key sorts
//   bytes(object)              the bytes of a plain object, as a View of its
//                              size
//
// Each operation reads only the elements of its Views, so no operation
// reads past a View.  A position is an index below the count of its View.
// Each operation is total: a search that finds nothing gives an empty
// Option, and no operation ends the process.  A predicate and a key do not
// throw, and a scan calls each one time for each element, in order.

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>
#include <foundation/core/Text.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace foundation::core {

namespace detail {

// An element that equal and find compare: == gives a bool and does not
// throw.
template <class T>
concept ComparableElement = requires(T const& left, T const& right) {
    { left == right } noexcept -> std::same_as<bool>;
};

// An element whose == is the compare of its bytes: an integer or an
// enumeration with no padding bit.  equal compares a run of such elements
// with one memcmp.
template <class T>
concept BytewiseEqualElement =
    (std::is_integral_v<T> || std::is_enum_v<T>) && std::has_unique_object_representations_v<T>;

// A predicate of a scan: it takes an element as a const reference, gives a
// bool and does not throw.
template <class Predicate, class T>
concept ElementPredicate =
    std::is_nothrow_invocable_v<Predicate&, T const&> && std::same_as<std::invoke_result_t<Predicate&, T const&>, bool>;

// A key of an ordered search: an integer or an enumeration, so the order of
// the keys is total.  A floating key is refused, because a NaN is in no
// order.
template <class K>
concept TotalKey = (std::is_integral_v<K> && !std::same_as<K, bool>) || std::is_enum_v<K>;

// A key function: it takes an element as a const reference, gives a total
// key and does not throw.
template <class Key, class T>
concept KeyOf = std::is_nothrow_invocable_v<Key&, T const&> && TotalKey<std::invoke_result_t<Key&, T const&>>;

}  // namespace detail

// True when the two Views hold the same count of elements, and each
// element equals the element at the same position of the other View.  An
// integer or an enumeration takes one memcmp, which is the compare of the
// raw form.  Complexity: O(n).
template <class T, std::size_t LeftExtent, class U, std::size_t RightExtent>
    requires std::same_as<std::remove_const_t<T>, std::remove_const_t<U>>
          && detail::ComparableElement<std::remove_const_t<T>>
[[nodiscard]] constexpr bool equal(View<T, LeftExtent> left, View<U, RightExtent> right) noexcept {
    using Element = std::remove_const_t<T>;
    std::size_t const count = left.size();
    if (count != right.size()) return false;
    if (count == 0) return true;
    Element const* const first = detail::first_of_(left);
    Element const* const second = detail::first_of_(right);
    if constexpr (detail::BytewiseEqualElement<Element>) {
        if !consteval {
            return __builtin_memcmp(first, second, count * sizeof(Element)) == 0;
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!(first[index] == second[index])) return false;
    }
    return true;
}

// True when the two texts hold the same characters in the same order.
[[nodiscard]] constexpr bool equal(TextView left, TextView right) noexcept { return left == right; }

// The position of the first element equal to value, or no value.
// Complexity: O(n).
template <class T, std::size_t Extent>
    requires detail::ComparableElement<std::remove_const_t<T>>
[[nodiscard]] constexpr Option<std::size_t> find(View<T, Extent> view,
                                                 std::type_identity_t<std::remove_const_t<T>> const& value) noexcept {
    std::remove_const_t<T> const* const first = detail::first_of_(view);
    for (std::size_t index = 0; index < view.size(); ++index) {
        if (first[index] == value) return Option<std::size_t>::some(index);
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
    std::remove_const_t<T> const* const first = detail::first_of_(view);
    for (std::size_t index = 0; index < view.size(); ++index) {
        if (predicate(first[index])) return Option<std::size_t>::some(index);
    }
    return none;
}

// True when predicate accepts each element.  An empty View gives true.
// The scan stops at the first element that predicate refuses.
template <class T, std::size_t Extent, class Predicate>
    requires detail::ElementPredicate<Predicate, std::remove_const_t<T>>
[[nodiscard]] constexpr bool all_of(View<T, Extent> view, Predicate predicate) noexcept {
    std::remove_const_t<T> const* const first = detail::first_of_(view);
    for (std::size_t index = 0; index < view.size(); ++index) {
        if (!predicate(first[index])) return false;
    }
    return true;
}

// True when predicate accepts at least one element.  An empty View gives
// false.  The scan stops at the first element that predicate accepts.
template <class T, std::size_t Extent, class Predicate>
    requires detail::ElementPredicate<Predicate, std::remove_const_t<T>>
[[nodiscard]] constexpr bool any_of(View<T, Extent> view, Predicate predicate) noexcept {
    std::remove_const_t<T> const* const first = detail::first_of_(view);
    for (std::size_t index = 0; index < view.size(); ++index) {
        if (predicate(first[index])) return true;
    }
    return false;
}

// The count of the elements that predicate accepts.  Complexity: O(n).
template <class T, std::size_t Extent, class Predicate>
    requires detail::ElementPredicate<Predicate, std::remove_const_t<T>>
[[nodiscard]] constexpr std::size_t count_if(View<T, Extent> view, Predicate predicate) noexcept {
    std::remove_const_t<T> const* const first = detail::first_of_(view);
    std::size_t count = 0;
    for (std::size_t index = 0; index < view.size(); ++index) {
        if (predicate(first[index])) ++count;
    }
    return count;
}

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

// The first position whose key is not below probe.  The View must be in
// the order of its keys, from the lowest key.  The result is a position
// from 0 to the count of the View, so a View in a different order gives a
// position of no meaning, and no read past the View.  The key function runs
// one time for each step.  Complexity: O(log n).
template <class T, std::size_t Extent, class Key>
    requires detail::KeyOf<Key, std::remove_const_t<T>>
[[nodiscard]] constexpr std::size_t
lower_bound_by_key(View<T, Extent> view, Key key,
                   std::invoke_result_t<Key&, std::remove_const_t<T> const&> probe) noexcept {
    std::remove_const_t<T> const* const first = detail::first_of_(view);
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
    return detail::view_over_<sizeof(T)>(
        static_cast<std::byte const*>(static_cast<void const*>(__builtin_addressof(object))));
}

template <ByteImageable T>
void bytes(T const&&) = delete("the bytes of a temporary object dangle at the end of the full expression");

}  // namespace foundation::core
