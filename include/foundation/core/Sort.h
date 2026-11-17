#pragma once

// The sorts and the ordered searches of the Region family.
//
//   Ascending                     the order of a sort when the caller gives
//                                 none: < for an integer and an enumeration,
//                                 and the total order of IEEE 754 for a float
//   sort_by_network<N>(view, less)
//                                 a sort of N elements with a fixed network of
//                                 compare-exchange steps, for N from 2 to 256,
//                                 in a ViewMut of the fixed extent N
//   plan_sort_network<N>()        the comparators of that network, in order
//   sort_stably(view, scratch, less)
//                                 a stable sort of a ViewMut of any count,
//                                 with a ViewMut of scratch of the same count
//                                 from the caller: a radix sort of the keys
//                                 for Ascending, and a merge sort for each
//                                 other order
//   find_lower_bound(view, value, less)
//                                 the first position whose element is not
//                                 below value, in a sorted View
//   find_equivalent(view, value, less)
//                                 the first position of an element equivalent
//                                 to value, or no value
//
// A sort writes a ViewMut of a frame door (foundation/core/Region.h), which
// it takes by value, so the run has one writer by its type.  A search reads
// a View.
//
// THE ORDER
//   A less function is a strict weak order on the elements: irreflexive,
//   transitive, and with an equivalence that is transitive.  For such an
//   order, the stable sort of a run is one permutation: the elements in the
//   order of less, and two equivalent elements in their order in the input.
//   So the result of sort_stably has no freedom, and each thread count, each
//   split and each host give the same bits.  The radix sort and the merge
//   sort give the same bits too.
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
//   sort_by_network of N makes the comparators of its plan: about
//   P * log2(P)^2 / 4 for P the next power of two.  sort_stably by Ascending
//   of at least stable_sort_radix_count elements reads the elements one time
//   for the counts of each byte of the keys.  Then it moves them one time for
//   each byte in which two keys differ.  Each other sort_stably makes
//   O(n log n) compares and copies: an insertion sort of each run of 32
//   elements, and merges of the runs between the View and the scratch.  A
//   merge of two runs that are in order is a copy.
//   find_lower_bound and find_equivalent make O(log n) compares.  No operation
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
[[nodiscard]] constexpr std::int32_t encode_total_order_key(float value) noexcept {
    auto const bits = __builtin_bit_cast(std::int32_t, value);
    return bits ^ static_cast<std::int32_t>(static_cast<std::uint32_t>(bits >> 31) >> 1);
}

[[nodiscard]] constexpr std::int64_t encode_total_order_key(double value) noexcept {
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
            return encode_total_order_key(left) < encode_total_order_key(right);
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
[[nodiscard]] consteval FixedArray<Comparator, sort_network_size<N>> plan_sort_network() noexcept {
    FixedArray<Comparator, sort_network_size<N>> steps{};
    static_cast<void>(detail::write_network_(N, first_of_(detail::ViewDoor::key(), steps.view())));
    return steps;
}

namespace detail {

template <std::size_t N>
inline constexpr FixedArray<Comparator, sort_network_size<N>> network_of = plan_sort_network<N>();

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

// Sorts the N elements with the network of N, by less.  The ViewMut has the
// fixed extent N, so the count is a fact of its type, as the frame door of a
// FixedArray or a split at a constant offset gives it.  Complexity:
// sort_network_size<N> compares, with no branch on the elements for an
// arithmetic element.
template <std::size_t N, class T, class Brand, class Less = Ascending>
    requires NetworkCount<N> && SortElement<T> && StrictOrder<Less, T>
constexpr void sort_by_network(ViewMut<T, Brand, N> elements, Less less = Less{}) noexcept {
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

// Copies the count elements from source into target, two runs that do not
// overlap.  At run time an element that the copy of Region.h takes moves as
// one copy of its bytes, so a long run takes no loop of single elements.  A
// constant evaluation, and each other element, take a loop.
template <class T>
constexpr void copy_run_(T const* source, std::size_t count, T* target) noexcept {
    if constexpr (RegionCopyElement<T>) {
        if !consteval {
            static_cast<void>(::foundation::core::copy(view_over_(target, count), view_over_(source, count)));
            return;
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        target[index] = source[index];
    }
}

// The merge of the sorted runs [0, middle) and [middle, count) of source
// into target.  An element of the second run goes first only when it is
// below the element of the first run, so the merge is stable.
//
// The stable merge of two runs is one permutation, so the merge takes it from
// the two ends at the same time.  The front takes the smaller of the first
// elements of the two runs, and the element of the first run on a tie.  The
// back takes the larger of the last elements, and the element of the second
// run on a tie.  So the front writes the first elements of the permutation,
// the back writes the last ones, and the bits are the bits of a merge from
// the front alone.  The two ends do not read the results of each other, so
// the processor runs their two chains of compares at the same time.  Each
// end takes as many elements as the shorter run holds, so no end reads past
// a run, and no element goes out two times.  A merge from the front then
// takes the elements between the two ends.
template <class T, class Less>
constexpr void merge_runs_(T const* source, std::size_t middle, std::size_t count, T* target, Less& less) noexcept {
    // Two runs are in order when the first element of the second run is not
    // below the last element of the first run.  Then the merge is a copy,
    // with one compare for the two runs.
    if (middle == 0 || middle == count || !less(source[middle], source[middle - 1])) {
        copy_run_(source, count, target);
        return;
    }
    std::size_t left = 0;
    std::size_t right = middle;
    std::size_t left_end = middle;
    std::size_t right_end = count;
    std::size_t out = 0;
    std::size_t out_end = count;
    std::size_t const shorter = middle < count - middle ? middle : count - middle;
    // One compare at each end selects the element and moves the positions.
    for (std::size_t step = 0; step < shorter; ++step) {
        bool const is_right_first = less(source[right], source[left]);
        target[out++] = source[is_right_first ? right : left];
        right += is_right_first ? 1 : 0;
        left += is_right_first ? 0 : 1;
        bool const is_left_last = less(source[right_end - 1], source[left_end - 1]);
        target[--out_end] = source[is_left_last ? left_end - 1 : right_end - 1];
        left_end -= is_left_last ? 1 : 0;
        right_end -= is_left_last ? 0 : 1;
    }
    while (left < left_end && right < right_end) {
        bool const is_right_first = less(source[right], source[left]);
        target[out++] = source[is_right_first ? right : left];
        right += is_right_first ? 1 : 0;
        left += is_right_first ? 0 : 1;
    }
    copy_run_(source + left, left_end - left, target + out);
    copy_run_(source + right, right_end - right, target + out + (left_end - left));
}

}  // namespace detail

// ── The radix sort ──────────────────────────────────────────────────────

// The count from which sort_stably sorts by the bytes of a key when the order
// is Ascending.  Below it, the merge sort makes fewer steps than the counts
// of the buckets.
inline constexpr std::size_t stable_sort_radix_count = 256;

namespace detail {

// The word of the key of T: 32 bits for an element of up to four bytes, so
// that no operation on a key promotes it, and 64 bits for eight bytes.
template <class T>
using radix_key_t = std::conditional_t<sizeof(T) <= 4, std::uint32_t, std::uint64_t>;

// The unsigned word of the size of T, for the bit cast of an element.
template <class T>
using radix_bits_t =
    std::conditional_t<sizeof(T) == 1, std::uint8_t,
                       std::conditional_t<sizeof(T) == 2, std::uint16_t,
                                          std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>>;

// True when the order of Ascending on T reads a sign bit: a signed integer,
// or an enumeration of a signed base.
template <class T>
[[nodiscard]] consteval bool has_signed_order_() noexcept {
    if constexpr (std::is_enum_v<T>) {
        return std::is_signed_v<std::underlying_type_t<T>>;
    } else {
        return std::is_signed_v<T>;
    }
}

// The key of value, whose order as an unsigned integer is the order of
// Ascending.  A float takes its total order key (encode_total_order_key)
// with the sign bit flipped.  An integer or an enumeration takes its bits,
// with the sign bit flipped when its order reads a sign.  Each key is a
// bijection of the bits, so two elements with one key have the same bits.
template <AscendingElement T>
    requires(sizeof(T) <= 8)
[[nodiscard]] CRUCIBLE_INLINE constexpr radix_key_t<T> radix_key_(T value) noexcept {
    using Key = radix_key_t<T>;
    constexpr Key sign = Key{1} << (8 * sizeof(T) - 1);
    if constexpr (std::same_as<T, float> || std::same_as<T, double>) {
        return __builtin_bit_cast(Key, encode_total_order_key(value)) ^ sign;
    } else if constexpr (has_signed_order_<T>()) {
        return Key{__builtin_bit_cast(radix_bits_t<T>, value)} ^ sign;
    } else {
        return Key{__builtin_bit_cast(radix_bits_t<T>, value)};
    }
}

// The count of the values of one byte of a key.
inline constexpr std::size_t radix_buckets = 256;

// Sorts the count elements from elements by the bytes of their keys, from the
// lowest byte, with one pass for each byte in which two elements differ.  A
// pass moves each element to its bucket in the order of its source, so the
// sort is stable, and the order of the keys is the order of Ascending.  The
// result is in elements, and the values of scratch have no meaning.  The
// count is at least 1.  Complexity: one read of the elements for the counts
// of each byte, and one read and one write of the elements for each pass.
template <AscendingElement T>
    requires(sizeof(T) <= 8)
constexpr void radix_sort_(T* elements, T* scratch, std::size_t count) noexcept {
    constexpr std::size_t bytes = sizeof(T);
    FixedArray<std::size_t, radix_buckets * bytes> counts{};
    std::size_t* const tally = first_of_(ViewDoor::key(), counts.view());
    for (std::size_t index = 0; index < count; ++index) {
        radix_key_t<T> const key = radix_key_(elements[index]);
        for (std::size_t byte = 0; byte < bytes; ++byte) {
            ++tally[byte * radix_buckets + ((key >> (8 * byte)) & 0xFFu)];
        }
    }
    radix_key_t<T> const first_key = radix_key_(elements[0]);
    T* source = elements;
    T* target = scratch;
    for (std::size_t byte = 0; byte < bytes; ++byte) {
        std::size_t* const bucket = tally + byte * radix_buckets;
        // A byte that each element shares moves no element.
        if (bucket[(first_key >> (8 * byte)) & 0xFFu] == count) continue;
        std::size_t start = 0;
        for (std::size_t value = 0; value < radix_buckets; ++value) {
            std::size_t const size = bucket[value];
            bucket[value] = start;
            start += size;
        }
        for (std::size_t index = 0; index < count; ++index) {
            T const element = source[index];
            target[bucket[(radix_key_(element) >> (8 * byte)) & 0xFFu]++] = element;
        }
        T* const sorted = target;
        target = source;
        source = sorted;
    }
    if (source != elements) {
        for (std::size_t index = 0; index < count; ++index) {
            elements[index] = source[index];
        }
    }
}

}  // namespace detail

// Sorts the elements by less, and keeps two equivalent elements in their
// order in the input.  The scratch holds as many elements as the elements,
// and its values after the call have no meaning.  Two counts that differ give
// LengthMismatch with no write.  The two are unique borrows, so they share no
// element.  With the order Ascending and at least stable_sort_radix_count
// elements, the sort is the radix sort of the keys of the elements.  Each
// other sort is the merge sort.  The two give the same bits (THE ORDER).
// Complexity: O(n) for the radix sort, with one pass for each byte of the
// element, and O(n log n) compares and copies for the merge sort.
template <class T, class Brand, std::size_t Extent, class ScratchBrand, std::size_t ScratchExtent,
          class Less = Ascending>
    requires SortElement<T> && StrictOrder<Less, T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> sort_stably(ViewMut<T, Brand, Extent> elements,
                                                                 ViewMut<T, ScratchBrand, ScratchExtent> scratch,
                                                                 Less less = Less{}) noexcept {
    std::size_t const count = elements.size();
    if (scratch.size() != count) return err(LengthMismatch{});
    T* source = first_of_(detail::ViewDoor::key(), elements);
    T* target = first_of_(detail::ViewDoor::key(), scratch);
    // The key of the radix sort has at most eight bytes.  A wider integer of a
    // GNU dialect, such as __int128, takes the merge sort.
    if constexpr (std::same_as<Less, Ascending> && sizeof(T) <= 8) {
        if (count >= stable_sort_radix_count) {
            detail::radix_sort_(source, target, count);
            return Unit{};
        }
    }
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
    if (is_in_scratch) detail::copy_run_(source, count, target);
    return Unit{};
}

// ── The ordered searches ────────────────────────────────────────────────

// The first position whose element is not below value, by less, in a View in
// the order of less.  The result is from 0 to the count of the View, so a View
// in a different order gives a position of no meaning, and no read past the
// View.  Complexity: O(log n) compares.
template <class T, std::size_t Extent, class Less = Ascending>
    requires SortElement<std::remove_const_t<T>> && StrictOrder<Less, std::remove_const_t<T>>
[[nodiscard]] constexpr std::size_t find_lower_bound(View<T, Extent> sorted,
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
[[nodiscard]] constexpr Option<std::size_t> find_equivalent(View<T, Extent> sorted,
                                                            std::type_identity_t<std::remove_const_t<T>> const& value,
                                                            Less less = Less{}) noexcept {
    std::size_t const position = find_lower_bound(sorted, value, less);
    if (position == sorted.size()) return none;
    if (less(value, first_of_(detail::ViewDoor::key(), sorted)[position])) return none;
    return Option<std::size_t>::some(position);
}

}  // namespace foundation::core
