#pragma once

// The sorts and the ordered searches of the Region family.
//
//   Ascending                     the order of a sort when the caller gives
//                                 none: < for an integer and an enumeration,
//                                 and the total order of IEEE 754 for a float
//   sort_network<N>(view, less)   a sort of N elements with a fixed network of
//                                 compare-exchange steps, for N from 2 to 256
//   sort_network_plan<N>()        the comparators of that network, in order
//   stable_sort(view, scratch, less)
//                                 a stable merge sort of a View of any count,
//                                 with scratch of the same count from the
//                                 caller
//   lower_bound(view, value, less)
//                                 the first position whose element is not
//                                 below value, in a sorted View
//   binary_search(view, value, less)
//                                 the first position of an element equivalent
//                                 to value, or no value
//
// THE ORDER
//   A less function is a strict weak order on the elements: irreflexive,
//   transitive, and with an equivalence that is transitive.  For such an
//   order, the stable sort of a run is one permutation: the elements in the
//   order of less, and two equivalent elements in their order in the input.
//   So the result of stable_sort has no freedom, and each thread count, each
//   split and each host give the same bits.
//
//   The network is the bitonic sort of the next power of two P >= N, in the
//   form in which each comparator puts the minimum at its lower position: the
//   first step of each merge compares position i with the position k - 1 - i
//   of its block of k.  A position at or past N holds an element above each
//   element, so a comparator that reads one changes nothing, and the network
//   of N drops it.  A test proves each network of N up to 20 on each input of
//   zeros and ones.  The network is not stable.  It gives one result for each
//   input, because its steps are fixed.
//
//   Ascending orders a float by the total order of IEEE 754: -NaN, -inf, the
//   negative numbers, -0, +0, the positive numbers, +inf, +NaN, and two NaNs of
//   one sign by their payloads.  So a NaN has one place, and -0 and +0 are not
//   equivalent.
//
// THE COSTS
//   sort_network of N makes the comparators of its plan: about
//   P * log2(P)^2 / 4 for P the next power of two.  stable_sort makes
//   O(n log n) compares and copies: an insertion sort of each run of 32
//   elements, and merges of the runs between the View and the scratch.
//   lower_bound and binary_search make O(log n) compares.  No operation
//   allocates.

#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

// The key of the total order of IEEE 754: a signed integer whose order is
// the total order of the float.  A negative float has each bit below its
// sign bit inverted, so a larger magnitude gives a smaller key.
[[nodiscard]] constexpr std::int32_t total_order_key(float value) noexcept {
    auto const bits = __builtin_bit_cast(std::int32_t, value);
    return bits ^ static_cast<std::int32_t>(static_cast<std::uint32_t>(bits >> 31) >> 1);
}

[[nodiscard]] constexpr std::int64_t total_order_key(double value) noexcept {
    auto const bits = __builtin_bit_cast(std::int64_t, value);
    return bits ^ static_cast<std::int64_t>(static_cast<std::uint64_t>(bits >> 63) >> 1);
}

// An element that Ascending orders: an integer other than bool, an
// enumeration, a float or a double.
template <class T>
concept AscendingElement = (std::is_integral_v<T> && !std::same_as<T, bool>) || std::is_enum_v<T>
                        || std::same_as<T, float> || std::same_as<T, double>;

// The order of a sort when the caller gives none.
struct Ascending {
    template <AscendingElement T>
    [[nodiscard]] constexpr bool operator()(T const& left, T const& right) const noexcept {
        if constexpr (std::same_as<T, float> || std::same_as<T, double>) {
            return total_order_key(left) < total_order_key(right);
        } else {
            return left < right;
        }
    }
};

// A less function of the elements: it takes two elements and gives a bool,
// with no throw.
template <class Less, class T>
concept StrictOrder = std::is_nothrow_invocable_v<Less&, T const&, T const&>
                   && std::same_as<std::invoke_result_t<Less&, T const&, T const&>, bool>;

// An element of a sort: an object that a copy and an assignment move, with no
// throw.
template <class T>
concept SortElement = std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T> && !std::is_array_v<T>
                   && std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_assignable_v<T>;

// ── The network ─────────────────────────────────────────────────────────

// One compare-exchange step: the minimum of the two elements goes to low,
// and the maximum to high.  low is below high.
struct Comparator {
    std::uint16_t low = 0;
    std::uint16_t high = 0;

    [[nodiscard]] friend constexpr bool operator==(Comparator const&, Comparator const&) noexcept = default;
};

// The count of the elements of a network: 2 to 256.
template <std::size_t N>
concept NetworkCount = N >= 2 && N <= 256;

namespace detail {

// Writes the comparators of the network of count elements into steps, or
// only counts them when steps is null.  Gives the count.
[[nodiscard]] constexpr std::size_t write_network_(std::size_t count, Comparator* steps) noexcept {
    std::size_t const width = bit_ceil(count).expect("a network has at most 256 elements");
    std::size_t written = 0;
    auto const emit = [&](std::size_t low, std::size_t high) constexpr noexcept {
        if (high >= count) return;
        if (steps != nullptr) {
            steps[written] = Comparator{static_cast<std::uint16_t>(low), static_cast<std::uint16_t>(high)};
        }
        ++written;
    };
    for (std::size_t block = 2; block <= width; block *= 2) {
        for (std::size_t start = 0; start < width; start += block) {
            for (std::size_t offset = 0; offset < block / 2; ++offset) {
                emit(start + offset, start + block - 1 - offset);
            }
        }
        for (std::size_t half = block / 4; half >= 1; half /= 2) {
            for (std::size_t start = 0; start < width; start += 2 * half) {
                for (std::size_t offset = 0; offset < half; ++offset) {
                    emit(start + offset, start + offset + half);
                }
            }
        }
    }
    return written;
}

}  // namespace detail

// The count of the comparators of the network of N elements.
template <std::size_t N>
    requires NetworkCount<N>
inline constexpr std::size_t sort_network_size = detail::write_network_(N, nullptr);

// The comparators of the network of N elements, in the order of THE ORDER.
// A step reads the elements after each earlier step that shares a position
// with it.
template <std::size_t N>
    requires NetworkCount<N>
[[nodiscard]] consteval FixedArray<Comparator, sort_network_size<N>> sort_network_plan() noexcept {
    FixedArray<Comparator, sort_network_size<N>> steps{};
    static_cast<void>(detail::write_network_(N, first_of_(detail::ViewDoor::key(), steps.view())));
    return steps;
}

namespace detail {

template <std::size_t N>
inline constexpr FixedArray<Comparator, sort_network_size<N>> network_of = sort_network_plan<N>();

// One compare-exchange.  The one compare gives the two selects, so the two
// outputs agree also for an order that a NaN breaks.
template <class T, class Less>
CRUCIBLE_INLINE constexpr void compare_exchange_(T* elements, Comparator step, Less& less) noexcept {
    T const low = elements[step.low];
    T const high = elements[step.high];
    bool const is_swapped = less(high, low);
    elements[step.low] = is_swapped ? high : low;
    elements[step.high] = is_swapped ? low : high;
}

}  // namespace detail

// Sorts the N elements with the network of N, by less.  Complexity:
// sort_network_size<N> compares, with no branch on the elements for an
// arithmetic element.
template <std::size_t N, class T, class Less = Ascending>
    requires NetworkCount<N> && SortElement<T> && StrictOrder<Less, T>
constexpr void sort_network(View<T, N> elements, Less less = Less{}) noexcept {
    T* const first = first_of_(detail::ViewDoor::key(), elements);
    for (Comparator const step : detail::network_of<N>) {
        detail::compare_exchange_(first, step, less);
    }
}

// ── The stable sort ─────────────────────────────────────────────────────

// The count of the elements of each run that the insertion sort orders
// before the first merge.
inline constexpr std::size_t stable_sort_run = 32;

namespace detail {

// The insertion sort of [first, first + count): stable, because an element
// moves only past elements that are greater.
template <class T, class Less>
constexpr void insertion_sort_(T* first, std::size_t count, Less& less) noexcept {
    for (std::size_t index = 1; index < count; ++index) {
        T const moving = first[index];
        std::size_t position = index;
        while (position > 0 && less(moving, first[position - 1])) {
            first[position] = first[position - 1];
            --position;
        }
        first[position] = moving;
    }
}

// The merge of the sorted runs [0, middle) and [middle, count) of source
// into target.  An element of the second run goes first only when it is
// below the element of the first run, so the merge is stable.
template <class T, class Less>
constexpr void merge_runs_(T const* source, std::size_t middle, std::size_t count, T* target, Less& less) noexcept {
    std::size_t left = 0;
    std::size_t right = middle;
    std::size_t out = 0;
    while (left < middle && right < count) {
        if (less(source[right], source[left])) {
            target[out++] = source[right++];
        } else {
            target[out++] = source[left++];
        }
    }
    while (left < middle) {
        target[out++] = source[left++];
    }
    while (right < count) {
        target[out++] = source[right++];
    }
}

}  // namespace detail

// Sorts the elements by less, and keeps two equivalent elements in their
// order in the input.  The scratch holds as many elements as the View, and
// its values after the call have no meaning.  Two counts that differ give
// LengthMismatch with no write.  The two Views must not overlap.
// Complexity: O(n log n) compares and copies.
template <class T, class Less = Ascending>
    requires SortElement<T> && StrictOrder<Less, T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> stable_sort(View<T> elements, View<T> scratch,
                                                                 Less less = Less{}) noexcept {
    std::size_t const count = elements.size();
    if (scratch.size() != count) return err(LengthMismatch{});
    T* source = first_of_(detail::ViewDoor::key(), elements);
    T* target = first_of_(detail::ViewDoor::key(), scratch);
    for (std::size_t start = 0; start < count; start += stable_sort_run) {
        std::size_t const size = count - start < stable_sort_run ? count - start : stable_sort_run;
        detail::insertion_sort_(source + start, size, less);
    }
    bool is_in_scratch = false;
    for (std::size_t width = stable_sort_run; width < count; width *= 2) {
        for (std::size_t start = 0; start < count; start += 2 * width) {
            std::size_t const rest = count - start;
            std::size_t const size = rest < 2 * width ? rest : 2 * width;
            std::size_t const middle = rest < width ? rest : width;
            detail::merge_runs_(source + start, middle, size, target + start, less);
        }
        T* const merged = target;
        target = source;
        source = merged;
        is_in_scratch = !is_in_scratch;
    }
    if (is_in_scratch) {
        for (std::size_t index = 0; index < count; ++index) {
            target[index] = source[index];
        }
    }
    return Unit{};
}

// ── The ordered searches ────────────────────────────────────────────────

// The first position whose element is not below value, by less, in a View in
// the order of less.  The result is from 0 to the count of the View, so a View
// in a different order gives a position of no meaning, and no read past the
// View.  Complexity: O(log n) compares.
template <class T, std::size_t Extent, class Less = Ascending>
    requires SortElement<std::remove_const_t<T>> && StrictOrder<Less, std::remove_const_t<T>>
[[nodiscard]] constexpr std::size_t lower_bound(View<T, Extent> sorted,
                                                std::type_identity_t<std::remove_const_t<T>> const& value,
                                                Less less = Less{}) noexcept {
    std::remove_const_t<T> const* const first = first_of_(detail::ViewDoor::key(), sorted);
    std::size_t low = 0;
    std::size_t high = sorted.size();
    while (low < high) {
        std::size_t const middle = low + (high - low) / 2;
        if (less(first[middle], value)) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

// The first position of an element equivalent to value, by less, in a View in
// the order of less, or no value.  Complexity: O(log n) compares.
template <class T, std::size_t Extent, class Less = Ascending>
    requires SortElement<std::remove_const_t<T>> && StrictOrder<Less, std::remove_const_t<T>>
[[nodiscard]] constexpr Option<std::size_t> binary_search(View<T, Extent> sorted,
                                                          std::type_identity_t<std::remove_const_t<T>> const& value,
                                                          Less less = Less{}) noexcept {
    std::size_t const position = lower_bound(sorted, value, less);
    if (position == sorted.size()) return none;
    if (less(value, first_of_(detail::ViewDoor::key(), sorted)[position])) return none;
    return Option<std::size_t>::some(position);
}

}  // namespace foundation::core
