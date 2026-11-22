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
//   order_by_key(project), order_by_key_descending(project)
//                                 the order of the elements by the key that
//                                 a data member or a function gives
//                                 (KeyOrder), in each direction
//   sort_stably(view, scratch, less)
//                                 a stable sort of a ViewMut of any count,
//                                 with a ViewMut of scratch of the same count
//                                 from the caller: a radix sort of the keys
//                                 for Ascending and for an order by a key,
//                                 and driftsort for each other order
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
//   split and each host give the same bits.  The radix sort and driftsort
//   give the same bits too.
//
//   A less function that is not a strict weak order, such as < on floats
//   with a NaN, breaks the precondition of sort_stably.  Each read and each
//   write of the sort then stays in the run and its scratch, and a merge
//   that sees such an order ends the process (CRUCIBLE_FATAL_INVARIANT).
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
//   or by an order by a key of at most eight bytes, of at least
//   stable_sort_radix_count elements, reads the elements one time for the
//   counts of each digit of the keys: a byte, or 11 bits of a key of four
//   bytes in a middle range of counts (THE DIGITS).  Then it moves them one
//   time for each digit in which two keys differ.  At run time a float or a
//   double first takes the bits of its key in its place, and the bits of the
//   float come back after the last pass.  Each other sort_stably is driftsort
//   (detail::StableSort): O(n log n) compares and copies in the worst case,
//   O(n log k) for k distinct values, and n - 1 compares for a run that is in
//   order or strictly descending.  Its loops over the elements have no branch
//   on an element, and the compares of a partition and of a scan of a run go
//   to vector lanes when less allows it.
//   find_lower_bound and find_equivalent make O(log n) compares.  No operation
//   allocates.

#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/core/Bits.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Region.h>
#include <foundation/core/Side.h>

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

// ── The order by a key ──────────────────────────────────────────────────

// The direction of an order by a key.
enum class KeyDirection : std::uint8_t {
    ascending,  // the order of Ascending on the keys
    descending,  // the reverse of the order of Ascending on the keys
};

namespace detail {

// The key of an element: the data member that project names, or the result
// of project.
template <class Project, class T>
[[nodiscard]] CRUCIBLE_INLINE constexpr decltype(auto) project_key_(Project const& project, T const& element) noexcept {
    if constexpr (std::is_member_object_pointer_v<Project>) {
        return (element.*project);
    } else {
        return project(element);
    }
}

template <class Project, class T>
using projected_key_t =
    std::remove_cvref_t<decltype(project_key_(std::declval<Project const&>(), std::declval<T const&>()))>;

}  // namespace detail

// A projection of an element of T to its key: a pointer to a data member of
// T, or a function of a T const& with no throw, whose key is an element that
// Ascending orders.
template <class Project, class T>
concept KeyProjection =
    (std::is_member_object_pointer_v<Project> || std::is_nothrow_invocable_v<Project const&, T const&>)
    && AscendingElement<detail::projected_key_t<Project, T>>;

// The order of the elements by their keys: Ascending on the keys, or its
// reverse.  Two elements of one key are equivalent, so a stable sort keeps
// them in their order of input in each direction.  order_by_key and
// order_by_key_descending make one.  From stable_sort_radix_count elements,
// sort_stably sorts by the bytes of the keys when a key has at most eight
// bytes, as it does for Ascending, and it gives the bits of the comparator
// form of the same order (THE ORDER).
template <class Project, KeyDirection Direction>
class KeyOrder final {
public:
    static constexpr KeyDirection direction = Direction;

    constexpr explicit KeyOrder(Project project) noexcept : project_{project} {}

    template <class T>
        requires KeyProjection<Project, T>
    [[nodiscard]] constexpr bool operator()(T const& left, T const& right) const noexcept {
        if constexpr (Direction == KeyDirection::ascending) {
            return Ascending{}(detail::project_key_(project_, left), detail::project_key_(project_, right));
        } else {
            return Ascending{}(detail::project_key_(project_, right), detail::project_key_(project_, left));
        }
    }

    // The key of an element.
    template <class T>
        requires KeyProjection<Project, T>
    [[nodiscard]] constexpr detail::projected_key_t<Project, T> key_of(T const& element) const noexcept {
        return detail::project_key_(project_, element);
    }

private:
    Project project_{};
};

// The ascending order of the keys that project gives.
template <class Project>
[[nodiscard]] constexpr KeyOrder<Project, KeyDirection::ascending> order_by_key(Project project) noexcept {
    return KeyOrder<Project, KeyDirection::ascending>{project};
}

// The descending order of the keys that project gives.
template <class Project>
[[nodiscard]] constexpr KeyOrder<Project, KeyDirection::descending> order_by_key_descending(Project project) noexcept {
    return KeyOrder<Project, KeyDirection::descending>{project};
}

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
    static_cast<void>(
        detail::write_network_(N, first_of_(detail::BaseLayerRead{}, frame_run_(detail::FrameDoor::key(), steps))));
    return steps;
}

namespace detail {

template <std::size_t N>
inline constexpr FixedArray<Comparator, sort_network_size<N>> network_of = plan_sort_network<N>();

// THE SELECTS.  Each compare-exchange, merge and small sort below picks a
// position by a mask: `left + ((right - left) & (0 - bit))` is left when the
// bit is 0 and right when it is 1, with unsigned arithmetic that wraps.  The
// compiler then emits an and, and no branch on an element.  A select of a
// float element in a vector register has no cmov, so GCC makes it a branch,
// and on random runs that branch misses on about one half of the steps.
//
// The bit of each select comes through select_bit_, which hides the value of
// the bit from the optimizer.  A plain bit lets GCC turn the mask into a
// conditional move on the compare.  Its pass isel then gives each conditional
// move its own copy of the compare, and -fharden-compares checks each copy:
// two compares and two checks for each step.  The hidden bit keeps one copy
// and one check for each compare, and each mask stays arithmetic.

// The bit of a select as a word whose value the optimizer does not know: an
// empty asm reads and writes it in a register.  A constant evaluation reads
// the plain bit.  The bit is the result of a less function, so the parameter
// is a deduced bool.
template <std::same_as<bool> Bit>
[[nodiscard]] CRUCIBLE_INLINE constexpr std::size_t select_bit_(Bit bit) noexcept {
    std::size_t word = static_cast<std::size_t>(bit);
    if !consteval {
        __asm__("" : "+r"(word));
    }
    return word;
}

// One compare-exchange.  The one compare gives the two selects, so the two
// outputs agree also for an order that a NaN breaks.  Each select picks the
// position of its element by a mask (THE SELECTS), so the exchange has no
// branch on an element.
template <class T, class Less>
CRUCIBLE_INLINE constexpr void compare_exchange_(T* elements, Comparator step, Less& less) noexcept {
    std::size_t const low = step.low;
    std::size_t const high = step.high;
    std::size_t const is_swapped = select_bit_(less(elements[high], elements[low]));
    std::size_t const shift = (high - low) & (std::size_t{0} - is_swapped);
    T const smaller = elements[low + shift];
    T const larger = elements[high - shift];
    elements[low] = smaller;
    elements[high] = larger;
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
    T* const first = first_of_(detail::BaseLayerRead{}, elements);
    for (Comparator const step : detail::network_of<N>) {
        detail::compare_exchange_(first, step, less);
    }
}

// ── The stable sort ─────────────────────────────────────────────────────

// The largest count of a run that the small sort of sort_stably orders: the
// quicksort stops at it, and the merge tree in its eager mode sorts runs of
// it.
inline constexpr std::size_t stable_sort_run = 32;

namespace side {

// The scratch of a stable sort: a run of the count of the elements, which the
// sort writes between its passes.
struct SortScratch final {};

}  // namespace side

namespace detail {

// A count or an offset of the elements of a run of a stable sort.
using SortCount = Quantity<unit::Element, std::size_t>;

// The largest count that sort_stably orders with an insertion sort alone.
inline constexpr std::size_t stable_sort_insertion_count = 20;

// The shortest run of the input that the merge tree keeps as one sorted run
// in an input of up to 64 * 64 elements.  A longer input takes about the
// square root of its count.  Each sorted run forces merges, so a short one
// does not pay for them.
inline constexpr std::size_t stable_sort_root_run = 64;

// The height of the stack of the merge tree: one run for each depth from 0
// to 64, and the empty run at the bottom.
inline constexpr std::size_t merge_tree_height = 66;

// The elements of a block of the partition, whose compares the compiler can
// put in the lanes of one vector of 512 bits for 32-bit elements.
inline constexpr std::size_t partition_block = 16;

// The elements of a block of the scan of a run of the input.
inline constexpr std::size_t run_scan_block = 16;

// The order of the run at the start of a part of the input.
enum class RunOrder : std::uint8_t {
    ascending,  // each element is not below the element before it
    descending,  // each element is below the element before it
};

// The run at the start of a part of the input.
struct InputRun {
    std::size_t count = 0;
    RunOrder order = RunOrder::ascending;
};

// Whether a run of the merge tree is in order, or holds elements that a
// quicksort orders before the run takes part in a merge.
enum class RunState : std::uint8_t {
    sorted,
    unsorted,
};

// One run of the merge tree.
struct TreeRun {
    std::size_t count = 0;
    RunState state = RunState::sorted;
};

// Two runs of the merge tree, the left one before the right one.
struct TreeRuns {
    TreeRun left{};
    TreeRun right{};
};

// Whether the merge tree sorts each short run of its input when it makes the
// run, or keeps the run for the quicksort of a longer run.
enum class RunMode : std::uint8_t {
    eager,
    lazy,
};

// The part of a partition that takes the pivot and the elements equivalent to
// it.  The usual partition gives them to the right part.  When the pivot of a
// run is equivalent to the pivot that made the run, the partition gives them
// to the left part, which then holds only such elements.
enum class PivotSide : std::uint8_t {
    left,
    right,
};

// The run of count elements from first.  The caller proves the elements live,
// and first not null when the count is above zero.
template <class T>
[[nodiscard]] CRUCIBLE_INLINE constexpr View<T> run_of_(T* first, SortCount count) noexcept {
    return View<T>{ViewDoor::key(), first, count.raw()};
}

// Copies the elements of source into target, two runs of one count that do
// not overlap.  At run time an element that the copy of Region.h takes moves
// as one copy of its bytes.  A constant evaluation, and each other element,
// take a loop.
template <class T>
constexpr void copy_run_(View<T const> source, View<T> target) noexcept {
    if constexpr (RegionCopyElement<T>) {
        if !consteval {
            static_cast<void>(::foundation::core::copy(target, source));
            return;
        }
    }
    T const* const from = first_of_(BaseLayerRead{}, source);
    T* const to = first_of_(BaseLayerRead{}, target);
    for (std::size_t index = 0; index < source.size(); ++index) {
        to[index] = from[index];
    }
}

// Moves the last element of a run into the sorted elements before it.  The
// element moves only past greater elements, so the insertion is stable.
// Complexity: one compare and one copy for each element that it passes.
template <class T, class Less>
constexpr void insert_last_(View<T> run, Less& less) noexcept {
    T* const first = first_of_(BaseLayerRead{}, run);
    std::size_t position = run.size() - 1;
    if (!less(first[position], first[position - 1])) return;
    T const moving = first[position];
    do {
        first[position] = first[position - 1];
        --position;
    } while (position > 0 && less(moving, first[position - 1]));
    first[position] = moving;
}

// The insertion sort of a run.  Complexity: O(n^2) compares and copies.
template <class T, class Less>
constexpr void insert_each_(View<T> run, Less& less) noexcept {
    T* const first = first_of_(BaseLayerRead{}, run);
    for (std::size_t count = 2; count <= run.size(); ++count) {
        insert_last_(run_of_(first, SortCount{count}), less);
    }
}

// Sorts the four elements of source into target, stably, with five compares:
// sort4_stable of driftsort.  The first two compares order the pairs (0, 1)
// and (2, 3), the next two find the minimum and the maximum, and the last one
// orders the two elements between them.  Each tie keeps the element of the
// lower position first.
template <class T, class Less>
CRUCIBLE_INLINE constexpr void sort_four_(View<T const, 4> source, View<T, 4> target, Less& less) noexcept {
    T const* const from = first_of_(BaseLayerRead{}, source);
    T* const to = first_of_(BaseLayerRead{}, target);
    std::size_t const first_pair = select_bit_(less(from[1], from[0]));
    std::size_t const second_pair = select_bit_(less(from[3], from[2]));
    std::size_t const low_a = first_pair;
    std::size_t const high_a = first_pair ^ 1u;
    std::size_t const low_b = 2 + second_pair;
    std::size_t const high_b = 2 + (second_pair ^ 1u);
    std::size_t const is_low_b = select_bit_(less(from[low_b], from[low_a]));
    std::size_t const is_high_a = select_bit_(less(from[high_b], from[high_a]));
    std::size_t const smallest = low_a + ((low_b - low_a) & (std::size_t{0} - is_low_b));
    std::size_t const largest = high_b + ((high_a - high_b) & (std::size_t{0} - is_high_a));
    // The two elements between the minimum and the maximum.  The two compares
    // of the minimum and the maximum tell which of them comes first in the
    // input, so a tie keeps that order.
    std::size_t const inner_b = high_a + ((low_b - high_a) & (std::size_t{0} - is_high_a));
    std::size_t const inner_a = low_b + ((high_a - low_b) & (std::size_t{0} - is_low_b));
    std::size_t const unknown_left = inner_b + ((low_a - inner_b) & (std::size_t{0} - is_low_b));
    std::size_t const unknown_right = inner_a + ((high_b - inner_a) & (std::size_t{0} - is_high_a));
    std::size_t const is_swapped = select_bit_(less(from[unknown_right], from[unknown_left]));
    std::size_t const middle_low = unknown_left + ((unknown_right - unknown_left) & (std::size_t{0} - is_swapped));
    std::size_t const middle_high = unknown_right + ((unknown_left - unknown_right) & (std::size_t{0} - is_swapped));
    to[0] = from[smallest];
    to[1] = from[middle_low];
    to[2] = from[middle_high];
    to[3] = from[largest];
}

// Merges the sorted halves [0, count / 2) and [count / 2, count) of source
// into target, from the two ends at the same time: the bidirectional merge
// of quadsort and driftsort.  The front takes the smaller of the first
// elements of the two halves, and the element of the left half on a tie.  The
// back takes the larger of the last elements, and the element of the right
// half on a tie.  Each end takes count / 2 elements, and the front takes the
// middle element of an odd count, so the result is the stable merge, and the
// two chains of compares run at the same time.  Each read stays in source for
// each less function.  A less function that is not a strict weak order can
// make the two ends take one element two times: the check at the end then
// ends the process.  Complexity: count - 1 compares at most, and count copies.
template <class T, class Less>
constexpr void merge_halves_(View<T const> source, View<T> target, Less& less) noexcept {
    std::size_t const count = source.size();
    std::size_t const half = count / 2;
    T const* const from = first_of_(BaseLayerRead{}, source);
    T* const to = first_of_(BaseLayerRead{}, target);
    std::size_t left = 0;
    std::size_t right = half;
    std::size_t out = 0;
    // The positions of the back wrap below zero only after the last step.
    std::size_t left_back = half - 1;
    std::size_t right_back = count - 1;
    std::size_t out_back = count - 1;
    for (std::size_t step = 0; step < half; ++step) {
        std::size_t const is_right = select_bit_(less(from[right], from[left]));
        to[out] = from[left + ((right - left) & (std::size_t{0} - is_right))];
        ++out;
        right += is_right;
        left += 1 - is_right;
        std::size_t const is_left = select_bit_(less(from[right_back], from[left_back]));
        to[out_back] = from[right_back + ((left_back - right_back) & (std::size_t{0} - is_left))];
        --out_back;
        left_back -= is_left;
        right_back -= 1 - is_left;
    }
    std::size_t const left_end = left_back + 1;
    std::size_t const right_end = right_back + 1;
    if (count % 2 != 0) {
        std::size_t const is_left = select_bit_(left < left_end);
        to[out] = from[right + ((left - right) & (std::size_t{0} - is_left))];
        left += is_left;
        right += 1 - is_left;
    }
    CRUCIBLE_FATAL_INVARIANT(left == left_end && right == right_end);
}

// Merges the sorted runs [0, middle) and [middle, count) of source into
// target, of the same count, from the two ends at the same time.  Each end
// takes as many elements as the shorter run holds, by the ties of
// merge_halves_.  A merge from the front then takes the elements between the
// two ends, and the rest of one run is a copy.  The result is the stable
// merge.  A less function that is not a strict weak order can make the two
// ends cross: the check after them then ends the process.  Complexity:
// count - 1 compares at most, and count copies.
template <class T, class Less>
constexpr void merge_two_ends_(View<T const> source, SortCount middle, View<T> target, Less& less) noexcept {
    std::size_t const count = source.size();
    T const* const from = first_of_(BaseLayerRead{}, source);
    T* const to = first_of_(BaseLayerRead{}, target);
    std::size_t left = 0;
    std::size_t right = middle.raw();
    std::size_t left_end = middle.raw();
    std::size_t right_end = count;
    std::size_t out = 0;
    std::size_t out_end = count;
    std::size_t const shorter = left_end < count - left_end ? left_end : count - left_end;
    for (std::size_t step = 0; step < shorter; ++step) {
        std::size_t const is_right = select_bit_(less(from[right], from[left]));
        to[out] = from[left + ((right - left) & (std::size_t{0} - is_right))];
        ++out;
        right += is_right;
        left += 1 - is_right;
        std::size_t const is_left = select_bit_(less(from[right_end - 1], from[left_end - 1]));
        --out_end;
        to[out_end] = from[right_end - 1 + ((left_end - right_end) & (std::size_t{0} - is_left))];
        left_end -= is_left;
        right_end -= 1 - is_left;
    }
    CRUCIBLE_FATAL_INVARIANT(left <= left_end && right <= right_end);
    while (left < left_end && right < right_end) {
        std::size_t const is_right = select_bit_(less(from[right], from[left]));
        to[out] = from[left + ((right - left) & (std::size_t{0} - is_right))];
        ++out;
        right += is_right;
        left += 1 - is_right;
    }
    copy_run_(run_of_(from + left, SortCount{left_end - left}), run_of_(to + out, SortCount{left_end - left}));
    out += left_end - left;
    copy_run_(run_of_(from + right, SortCount{right_end - right}), run_of_(to + out, SortCount{right_end - right}));
}

// THE STABLE SORT OF AN ORDER OF THE CALLER is driftsort, the stable sort of
// the Rust standard library (Orson Peters and Lukas Bergdoll, 2024).
//   - A run of up to stable_sort_insertion_count elements takes an insertion
//     sort.
//   - The merge tree scans the input for runs in order, and for strictly
//     descending runs, which it reverses.  A run of at least the square root
//     of the count stays a sorted run.  The input between such runs becomes
//     unsorted runs.  The powersort rule of Munro and Wild gives each split
//     point between two runs a depth, and a stack merges the runs in the
//     order of the depths.  A merge of two unsorted runs is one longer
//     unsorted run, so a random input becomes one unsorted run.
//   - A merge with a sorted run first sorts each unsorted run by a stable
//     quicksort, then merges the two runs from the two ends.
//   - The stable quicksort takes the pseudomedian of samples of the run as its
//     pivot.  The partition writes the elements before the pivot to the front
//     of the scratch in their order, and the others to the back in the reverse
//     order, with no branch on an element, then copies the two parts back.
//     When a pivot is equivalent to the pivot that made its run, the partition
//     puts the equivalent elements to the left, and the sort of that part
//     ends: a run of k distinct values takes O(n log k).  After a budget of
//     2 log2(n) partitions on one path, a part takes the eager merge tree, so
//     the worst case is O(n log n).
//   - A part of up to stable_sort_run elements takes the small sort: two
//     stable sorts of eight by sort4_stable and a merge, insertions to two
//     sorted halves, and a merge of the halves from the two ends.
// A run of the elements and its scratch are the parts of the elements and of
// the scratch at the same offset, so each step takes its run alone.
template <class T, class Less>
class StableSort final {
public:
    constexpr StableSort(View<T> elements, Side<side::SortScratch, View<T>> const& scratch, Less& less) noexcept
        : elements_{first_of_(BaseLayerRead{}, elements)},
          scratch_{first_of_(BaseLayerRead{}, scratch.value())},
          less_{less} {}

    // Sorts a run of the elements.
    constexpr void sort(View<T> run) noexcept {
        if (run.size() <= stable_sort_insertion_count) {
            insert_each_(run, less_);
            return;
        }
        sort_runs_(run, run.size() <= 2 * stable_sort_run ? RunMode::eager : RunMode::lazy);
    }

private:
    // The part of the scratch at the offset of a run of the elements.
    [[nodiscard]] constexpr T* scratch_of_(View<T> run) const noexcept {
        return scratch_ + (first_of_(BaseLayerRead{}, run) - elements_);
    }

    // The merge tree of a run (drift::sort of driftsort).
    constexpr void sort_runs_(View<T> run, RunMode mode) noexcept {
        std::size_t const count = run.size();
        if (count < 2) return;
        T* const first = first_of_(BaseLayerRead{}, run);
        // The scale of the depths: the ceiling of 2^62 / count.  A split
        // point between [a, b) and [b, c) has the depth of the highest bit
        // in which (a + b) * scale and (b + c) * scale differ.
        std::uint64_t const scale = ((std::uint64_t{1} << 62) + count - 1) / count;
        std::size_t good_run = count - count / 2 < stable_sort_root_run ? count - count / 2 : stable_sort_root_run;
        if (count > stable_sort_root_run * stable_sort_root_run) {
            // The square root of count: 2^((1 + log2 count) / 2), and one step
            // of Newton.
            int const shift = bit_width(count) / 2;
            good_run = ((std::size_t{1} << shift) + (count >> shift)) / 2;
        }
        FixedArray<TreeRun, merge_tree_height> runs{};
        FixedArray<std::uint8_t, merge_tree_height> depths{};
        TreeRun* const run_stack = first_of_(BaseLayerRead{}, frame_run_(FrameDoor::key(), runs));
        std::uint8_t* const depth_stack = first_of_(BaseLayerRead{}, frame_run_(FrameDoor::key(), depths));
        std::size_t height = 0;
        std::size_t scan = 0;
        TreeRun previous{};
        for (;;) {
            // The last pass takes an empty run of the depth 0, which merges
            // each run on the stack.
            TreeRun next{};
            std::uint8_t depth = 0;
            if (scan < count) {
                next = create_run_(run_of_(first + scan, SortCount{count - scan}), SortCount{good_run}, mode);
                std::uint64_t const left_point = scan - previous.count + scan;
                std::uint64_t const right_point = scan + scan + next.count;
                depth = static_cast<std::uint8_t>(countl_zero((scale * left_point) ^ (scale * right_point)));
            }
            while (height > 1 && depth_stack[height - 1] >= depth) {
                TreeRun const left = run_stack[height - 1];
                std::size_t const merged = left.count + previous.count;
                previous =
                    merge_tree_runs_(run_of_(first + scan - merged, SortCount{merged}), TreeRuns{left, previous});
                --height;
            }
            // Each depth on the stack is below the depth above it, and a depth
            // is at most 64.
            CRUCIBLE_FATAL_INVARIANT(height < merge_tree_height);
            run_stack[height] = previous;
            depth_stack[height] = depth;
            ++height;
            if (scan >= count) break;
            scan += next.count;
            previous = next;
        }
        if (previous.state == RunState::unsorted) sort_by_partitions_(run);
    }

    // The run at the start of rest: a sorted run of the input of good_run
    // elements or more, a run that the small sort sorts in the eager mode, or
    // an unsorted run of good_run elements in the lazy mode.
    [[nodiscard]] constexpr TreeRun create_run_(View<T> rest, SortCount good_run, RunMode mode) noexcept {
        std::size_t const count = rest.size();
        T* const first = first_of_(BaseLayerRead{}, rest);
        if (count >= good_run.raw()) {
            InputRun const found = find_input_run_(View<T const>{rest});
            if (found.count >= good_run.raw()) {
                if (found.order == RunOrder::descending) reverse_run_(run_of_(first, SortCount{found.count}));
                return TreeRun{found.count, RunState::sorted};
            }
        }
        if (mode == RunMode::eager) {
            std::size_t const eager = count < stable_sort_run ? count : stable_sort_run;
            sort_small_(run_of_(first, SortCount{eager}));
            return TreeRun{eager, RunState::sorted};
        }
        return TreeRun{count < good_run.raw() ? count : good_run.raw(), RunState::unsorted};
    }

    // The run at the start of rest, in order or strictly descending.  A
    // strictly descending run has no two equivalent elements, so its reverse
    // is its stable sort.  Complexity: one compare for each element of the
    // run, and at most run_scan_block more.
    [[nodiscard]] constexpr InputRun find_input_run_(View<T const> rest) const noexcept {
        std::size_t const count = rest.size();
        if (count < 2) return InputRun{count, RunOrder::ascending};
        T const* const first = first_of_(BaseLayerRead{}, rest);
        if (less_(first[1], first[0])) return InputRun{extend_run_<RunOrder::descending>(rest), RunOrder::descending};
        return InputRun{extend_run_<RunOrder::ascending>(rest), RunOrder::ascending};
    }

    // The count of the run of the order Order at the start of rest, whose
    // first two elements have that order.  The first stable_sort_run
    // elements take one compare each, so a random input, whose runs end in a
    // few elements, pays no block.  Then the compares of a block of elements
    // go to a list of bits in a loop of their own, so the compiler can
    // compare the block in vector lanes when less allows it.
    template <RunOrder Order>
    [[nodiscard]] constexpr std::size_t extend_run_(View<T const> rest) const noexcept {
        std::size_t const count = rest.size();
        T const* const first = first_of_(BaseLayerRead{}, rest);
        // The element at position breaks the run when it is out of the order
        // of Order with the element before it.
        auto const is_break = [&](SortCount position) constexpr noexcept {
            if constexpr (Order == RunOrder::descending) {
                return !less_(first[position.raw()], first[position.raw() - 1]);
            } else {
                return less_(first[position.raw()], first[position.raw() - 1]);
            }
        };
        std::size_t length = 2;
        std::size_t const scalar_end = count < stable_sort_run ? count : stable_sort_run;
        while (length < scalar_end && !is_break(SortCount{length}))
            ++length;
        if (length < scalar_end) return length;
        while (length + run_scan_block <= count) {
            FixedArray<std::uint8_t, run_scan_block> breaks{};
            std::uint8_t* const is_broken = first_of_(BaseLayerRead{}, frame_run_(FrameDoor::key(), breaks));
            std::uint8_t is_any_broken = 0;
            for (std::size_t offset = 0; offset < run_scan_block; ++offset) {
                is_broken[offset] = static_cast<std::uint8_t>(is_break(SortCount{length + offset}));
                is_any_broken |= is_broken[offset];
            }
            if (is_any_broken != 0) {
                std::size_t offset = 0;
                while (is_broken[offset] == 0)
                    ++offset;
                return length + offset;
            }
            length += run_scan_block;
        }
        while (length < count && !is_break(SortCount{length}))
            ++length;
        return length;
    }

    // Reverses the order of the elements of a run.
    static constexpr void reverse_run_(View<T> run) noexcept {
        T* const first = first_of_(BaseLayerRead{}, run);
        std::size_t low = 0;
        std::size_t high = run.size();
        while (low + 1 < high) {
            --high;
            T const held = first[low];
            first[low] = first[high];
            first[high] = held;
            ++low;
        }
    }

    // The merge of two runs of the merge tree that fill merged.  Two unsorted
    // runs are one unsorted run, because the scratch holds the count of the
    // elements.  Each other pair takes a quicksort of each unsorted run, then
    // a merge (logical_merge of driftsort).
    [[nodiscard]] constexpr TreeRun merge_tree_runs_(View<T> merged, TreeRuns runs) noexcept {
        std::size_t const count = merged.size();
        if (runs.left.state == RunState::unsorted && runs.right.state == RunState::unsorted) {
            return TreeRun{count, RunState::unsorted};
        }
        T* const first = first_of_(BaseLayerRead{}, merged);
        if (runs.left.state == RunState::unsorted) sort_by_partitions_(run_of_(first, SortCount{runs.left.count}));
        if (runs.right.state == RunState::unsorted) {
            sort_by_partitions_(run_of_(first + runs.left.count, SortCount{runs.right.count}));
        }
        merge_(merged, SortCount{runs.left.count});
        return TreeRun{count, RunState::sorted};
    }

    // Merges the sorted runs [0, middle) and [middle, count) of a run in
    // place.  Two runs in order take one compare.  Each other pair copies the
    // run to its scratch and merges it back from the two ends.
    constexpr void merge_(View<T> run, SortCount middle) noexcept {
        std::size_t const count = run.size();
        T* const first = first_of_(BaseLayerRead{}, run);
        if (middle.raw() == 0 || middle.raw() >= count || !less_(first[middle.raw()], first[middle.raw() - 1])) {
            return;
        }
        View<T> const room = run_of_(scratch_of_(run), SortCount{count});
        copy_run_(View<T const>{run}, room);
        merge_two_ends_(View<T const>{room}, middle, run, less_);
    }

    // The stable quicksort of a run, with the budget of its depth.
    constexpr void sort_by_partitions_(View<T> run) noexcept {
        std::uint32_t const outer_budget = budget_;
        budget_ = 2 * static_cast<std::uint32_t>(bit_width(run.size() | 1u) - 1);
        partition_down_(run, Option<T>{none});
        budget_ = outer_budget;
    }

    // The stable quicksort of a run whose elements are not before ancestor,
    // the pivot of the partition that made the run, when it has one.  The left
    // part of each partition takes the next pass of the loop, and the right
    // part takes a call (quicksort of driftsort).
    constexpr void partition_down_(View<T> run, Option<T> ancestor) noexcept {
        for (;;) {
            std::size_t const count = run.size();
            if (count <= stable_sort_run) {
                sort_small_(run);
                return;
            }
            if (budget_ == 0) {
                sort_runs_(run, RunMode::eager);
                return;
            }
            --budget_;
            T* const first = first_of_(BaseLayerRead{}, run);
            T const pivot = first[choose_pivot_(View<T const>{run})];
            bool is_equal_partition =
                Option<T>{ancestor}.match([&](T ancestor_pivot) noexcept { return !less_(ancestor_pivot, pivot); },
                                          []() noexcept { return false; });
            std::size_t left_count = 0;
            if (!is_equal_partition) {
                left_count = partition_<PivotSide::right>(run, pivot);
                is_equal_partition = left_count == 0;
            }
            if (is_equal_partition) {
                // Each element of the left part is equivalent to the pivot.
                std::size_t const equal_count = partition_<PivotSide::left>(run, pivot);
                run = run_of_(first + equal_count, SortCount{count - equal_count});
                ancestor = Option<T>{none};
                continue;
            }
            std::uint32_t const path_budget = budget_;
            partition_down_(run_of_(first + left_count, SortCount{count - left_count}), Option<T>::some(pivot));
            budget_ = path_budget;
            run = run_of_(first, SortCount{left_count});
        }
    }

    // The position of the pivot of a run of more than stable_sort_run
    // elements: the median of the samples at 0, 4/8 and 7/8 of the run, and
    // for a run of 64 elements or more, the median of three such medians of
    // each sample, recursively (the pseudomedian of driftsort).
    [[nodiscard]] constexpr std::size_t choose_pivot_(View<T const> run) const noexcept {
        std::size_t const eighth = run.size() / 8;
        T const* const first = first_of_(BaseLayerRead{}, run);
        T const* const chosen =
            run.size() < 64 ? median_of_three_(first, first + eighth * 4, first + eighth * 7)
                            : median_of_samples_(first, first + eighth * 4, first + eighth * 7, SortCount{eighth});
        return static_cast<std::size_t>(chosen - first);
    }

    // The median of three samples of a run, as a pointer to one of them.  The
    // three pointers can swap with no change of the meaning: each order gives
    // a median of the three.
    [[nodiscard]] constexpr T const* median_of_three_(T const* first, T const* second, T const* third) const noexcept {
        bool const is_below_second = less_(*first, *second);
        bool const is_below_third = less_(*first, *third);
        // first lies between the other two.
        if (is_below_second != is_below_third) return first;
        // first is below both or not below both: the median is the smaller of
        // the two in the first case and the larger in the second.
        bool const is_second_below_third = less_(*second, *third);
        return is_second_below_third != is_below_second ? third : second;
    }

    // The median of the medians of the samples around three samples, each
    // sample with spread elements after it.  The three pointers can swap with
    // no change of the meaning.
    [[nodiscard]] constexpr T const* median_of_samples_(T const* first, T const* second, T const* third,
                                                        SortCount spread) const noexcept {
        if (spread.raw() * 8 >= 64) {
            std::size_t const eighth = spread.raw() / 8;
            SortCount const next{eighth};
            first = median_of_samples_(first, first + eighth * 4, first + eighth * 7, next);
            second = median_of_samples_(second, second + eighth * 4, second + eighth * 7, next);
            third = median_of_samples_(third, third + eighth * 4, third + eighth * 7, next);
        }
        return median_of_three_(first, second, third);
    }

    // Partitions a run by pivot, stably, and gives the count of the left
    // part.  An element goes left when it is before the pivot: less(element,
    // pivot) for PivotSide::right, and !less(pivot, element) for
    // PivotSide::left.  The scratch takes the left elements from its front in
    // their order, and the others from its back in the reverse order, then
    // the two parts go back to the run.
    //
    // Each element goes to the front cursor and to the rear cursor, and the
    // compare moves only the front cursor.  The rear cursor of the element at
    // index is count - 1 - index + front: the right elements so far are
    // index - front.  So the loop has no select and no branch on an element,
    // and the one compare feeds one add.  A second use of the compare, such
    // as a cursor that falls by the complement, lets GCC emit the reverse
    // compare, which -fharden-compares checks again.  The write to the
    // cursor of the other part lands on a slot that a later element fills:
    // before each write, the rear cursor is at or after the front cursor, the
    // left elements lie before the front cursor and the right elements after
    // the rear cursor.  Each element takes one slot for each less function,
    // so the run keeps its elements.  Complexity: count compares and
    // 3 * count copies.
    template <PivotSide Pivot>
    [[nodiscard]] constexpr std::size_t partition_(View<T> run, T const& pivot) noexcept {
        std::size_t const count = run.size();
        T* const first = first_of_(BaseLayerRead{}, run);
        T* const scratch = scratch_of_(run);
        std::size_t front = 0;
        std::size_t index = 0;
        // The compares of a block of elements go to a list of bits in a loop
        // of their own, so the compiler can compare the block in vector
        // lanes when less allows it.  The writes then read the bits.
        while (index + partition_block <= count) {
            FixedArray<std::uint8_t, partition_block> lefts{};
            std::uint8_t* const is_left = first_of_(BaseLayerRead{}, frame_run_(FrameDoor::key(), lefts));
            T const* const block = first + index;
            for (std::size_t offset = 0; offset < partition_block; ++offset) {
                if constexpr (Pivot == PivotSide::right) {
                    is_left[offset] = static_cast<std::uint8_t>(less_(block[offset], pivot));
                } else {
                    is_left[offset] = static_cast<std::uint8_t>(!less_(pivot, block[offset]));
                }
            }
            for (std::size_t offset = 0; offset < partition_block; ++offset) {
                scratch[front] = block[offset];
                scratch[count - 1 - (index + offset) + front] = block[offset];
                front += is_left[offset];
            }
            index += partition_block;
        }
        for (; index < count; ++index) {
            std::size_t is_left = 0;
            if constexpr (Pivot == PivotSide::right) {
                is_left = static_cast<std::size_t>(less_(first[index], pivot));
            } else {
                is_left = static_cast<std::size_t>(!less_(pivot, first[index]));
            }
            scratch[front] = first[index];
            scratch[count - 1 - index + front] = first[index];
            front += is_left;
        }
        copy_run_(run_of_(static_cast<T const*>(scratch), SortCount{front}), run_of_(first, SortCount{front}));
        for (std::size_t back = front; back < count; ++back) {
            first[back] = scratch[count - 1 - (back - front)];
        }
        return front;
    }

    // The small sort of a run of 2 to stable_sort_run elements, with its
    // scratch of the same count (small_sort_general of driftsort).  Each half
    // of the scratch takes a stable sort of the first eight or four elements
    // of its half of the run, then the insertion of the other elements of
    // that half.  The merge of the two halves from the two ends then writes
    // the run.
    constexpr void sort_small_(View<T> run) noexcept {
        std::size_t const count = run.size();
        if (count < 2) return;
        T* const first = first_of_(BaseLayerRead{}, run);
        T* const scratch = scratch_of_(run);
        std::size_t const half = count / 2;
        std::size_t sorted = 1;
        if (count >= 16) {
            sort_eight_(run_of_(first, SortCount{8}));
            sort_eight_(run_of_(first + half, SortCount{8}));
            sorted = 8;
        } else if (count >= 8) {
            sort_four_(View<T const, 4>{ViewDoor::key(), first}, View<T, 4>{ViewDoor::key(), scratch}, less_);
            sort_four_(View<T const, 4>{ViewDoor::key(), first + half}, View<T, 4>{ViewDoor::key(), scratch + half},
                       less_);
            sorted = 4;
        } else {
            scratch[0] = first[0];
            scratch[half] = first[half];
        }
        for (std::size_t length = sorted + 1; length <= half; ++length) {
            scratch[length - 1] = first[length - 1];
            insert_last_(run_of_(scratch, SortCount{length}), less_);
        }
        for (std::size_t length = sorted + 1; length <= count - half; ++length) {
            scratch[half + length - 1] = first[half + length - 1];
            insert_last_(run_of_(scratch + half, SortCount{length}), less_);
        }
        merge_halves_(View<T const>{run_of_(scratch, SortCount{count})}, run, less_);
    }

    // The stable sort of the eight elements of a run into its scratch: two
    // sorts of four in place, and their merge into the scratch (sort8_stable
    // of driftsort).
    constexpr void sort_eight_(View<T> run) noexcept {
        T* const first = first_of_(BaseLayerRead{}, run);
        sort_four_in_place_(View<T, 4>{ViewDoor::key(), first});
        sort_four_in_place_(View<T, 4>{ViewDoor::key(), first + 4});
        merge_halves_(View<T const>{run}, run_of_(scratch_of_(run), SortCount{8}), less_);
    }

    // The stable sort of four elements in place: a copy of the four, then
    // sort_four_ from the copy.
    constexpr void sort_four_in_place_(View<T, 4> run) noexcept {
        T* const first = first_of_(BaseLayerRead{}, run);
        FixedArray<T, 4> const held{first[0], first[1], first[2], first[3]};
        sort_four_(View<T const, 4>{held.view()}, run, less_);
    }

    T* elements_ = nullptr;
    T* scratch_ = nullptr;
    Less& less_;
    // The partitions that the current path of the quicksort can still make.
    std::uint32_t budget_ = 0;
};

}  // namespace detail

// ── The radix sort ──────────────────────────────────────────────────────

// The count from which sort_stably sorts by the bytes of a key when the order
// is Ascending.  Below it, driftsort makes fewer steps than the counts of the
// buckets.
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

// THE DIGITS.  The radix sort takes a digit of 8 bits of the key in each
// pass, or a digit of 11 bits for a key of four bytes: three passes of 2048
// buckets in place of four passes of 256.  The buckets of a pass cost a prefix
// sum, which a small run does not repay, and a large run scatters into more
// lines than the caches of a core hold.  So a key of four bytes takes digits of
// 11 bits from radix_wide_digit_count elements and below
// radix_wide_digit_bytes bytes of elements, and digits of 8 bits else.  The
// two digits give the same output, because a stable sort has one output.

// The runs whose key of four bytes takes digits of 11 bits: from
// radix_wide_digit_count elements, and below radix_wide_digit_bytes bytes of
// elements (2^21 elements of four bytes, 2^20 records of eight).  The counts
// of the buckets of such a run fit in 32 bits.
inline constexpr std::size_t radix_wide_digit_count = 4096;
inline constexpr std::size_t radix_wide_digit_bytes = std::size_t{1} << 23;

// Sorts a run by the Bytes low bytes of the key of each element, a digit of
// DigitBits bits in each pass, from the lowest digit, with one pass for each
// digit in which two elements differ.  key_of gives the key of an element: an
// unsigned word whose order is the order of the sort.  A pass moves each
// element to its bucket in the order of its source, so the sort is stable.
// The result is in the run, and the values of the scratch have no meaning.
// The run holds at least one element, and a count of a bucket fits in Tally.
// Complexity: one read of the run for the counts of each digit, and one read
// and one write of the run for each pass.
template <std::size_t Bytes, std::size_t DigitBits, class Tally, class T, class KeyOf>
constexpr void radix_sort_(View<T> run, Side<side::SortScratch, View<T>> const& scratch, KeyOf const& key_of) noexcept {
    constexpr std::size_t passes = (8 * Bytes + DigitBits - 1) / DigitBits;
    constexpr std::size_t buckets = std::size_t{1} << DigitBits;
    constexpr std::size_t digit_mask = buckets - 1;
    std::size_t const count = run.size();
    T* const elements = first_of_(BaseLayerRead{}, run);
    FixedArray<Tally, buckets * passes> counts{};
    Tally* const tally = first_of_(BaseLayerRead{}, frame_run_(FrameDoor::key(), counts));
    for (std::size_t index = 0; index < count; ++index) {
        auto const key = key_of(elements[index]);
        for (std::size_t pass = 0; pass < passes; ++pass) {
            ++tally[pass * buckets + ((key >> (DigitBits * pass)) & digit_mask)];
        }
    }
    auto const first_key = key_of(elements[0]);
    T* source = elements;
    T* target = first_of_(BaseLayerRead{}, scratch.value());
    for (std::size_t pass = 0; pass < passes; ++pass) {
        Tally* const bucket = tally + pass * buckets;
        // A digit that each element shares moves no element.
        if (bucket[(first_key >> (DigitBits * pass)) & digit_mask] == count) continue;
        Tally start = 0;
        for (std::size_t value = 0; value < buckets; ++value) {
            Tally const size = bucket[value];
            bucket[value] = start;
            start += size;
        }
        for (std::size_t index = 0; index < count; ++index) {
            T const element = source[index];
            target[bucket[(key_of(element) >> (DigitBits * pass)) & digit_mask]++] = element;
        }
        T* const sorted = target;
        target = source;
        source = sorted;
    }
    if (source != elements) copy_run_(run_of_(static_cast<T const*>(source), SortCount{count}), run);
}

// Sorts a run by the Bytes low bytes of the key of each element, with the
// digits of its count (THE DIGITS).
template <std::size_t Bytes, class T, class KeyOf>
constexpr void radix_sort_by_count_(View<T> run, Side<side::SortScratch, View<T>> const& scratch,
                                    KeyOf const& key_of) noexcept {
    if constexpr (Bytes == 4) {
        if (run.size() >= radix_wide_digit_count && run.size() < radix_wide_digit_bytes / sizeof(T)) {
            radix_sort_<Bytes, 11, std::uint32_t>(run, scratch, key_of);
            return;
        }
    }
    radix_sort_<Bytes, 8, std::size_t>(run, scratch, key_of);
}

// Writes the bits of the radix key of each float of a run in its place.
template <class T>
    requires std::same_as<T, float> || std::same_as<T, double>
constexpr void encode_radix_keys_(View<T> run) noexcept {
    T* const first = first_of_(BaseLayerRead{}, run);
    for (std::size_t index = 0; index < run.size(); ++index) {
        first[index] = __builtin_bit_cast(T, radix_key_(first[index]));
    }
}

// Writes back the float of the radix key in each place of a run: the inverse
// of encode_radix_keys_.  A flip of the sign bit gives the total order key,
// and the total order key of a total order key is the bits of the float.
template <class T>
    requires std::same_as<T, float> || std::same_as<T, double>
constexpr void decode_radix_keys_(View<T> run) noexcept {
    using Key = radix_key_t<T>;
    using Signed = std::conditional_t<sizeof(T) == 4, std::int32_t, std::int64_t>;
    constexpr Key sign = Key{1} << (8 * sizeof(T) - 1);
    T* const first = first_of_(BaseLayerRead{}, run);
    for (std::size_t index = 0; index < run.size(); ++index) {
        Key const key = __builtin_bit_cast(Key, first[index]) ^ sign;
        Key const flip = static_cast<Key>(__builtin_bit_cast(Signed, key) >> (8 * sizeof(T) - 1)) >> 1;
        first[index] = __builtin_bit_cast(T, key ^ flip);
    }
}

// True for an order by key whose key has at most eight bytes: an order that
// the radix sort can take.
template <class Less, class T>
inline constexpr bool is_radix_order_by_key_v = false;

template <class Project, KeyDirection Direction, class T>
    requires KeyProjection<Project, T>
inline constexpr bool is_radix_order_by_key_v<KeyOrder<Project, Direction>, T> =
    sizeof(projected_key_t<Project, T>) <= 8;

}  // namespace detail

// Sorts the elements by less, and keeps two equivalent elements in their
// order in the input.  The scratch holds as many elements as the elements,
// and its values after the call have no meaning.  Two counts that differ give
// LengthMismatch with no write.  The two are unique borrows, so they share no
// element.  With the order Ascending, or an order by a key of at most eight
// bytes (KeyOrder), and at least stable_sort_radix_count elements, the sort
// is the radix sort of the keys of the elements.  Each other sort is
// driftsort (detail::StableSort).  The two give the same bits (THE ORDER).
// Complexity: O(n) for the radix sort, with one pass for each digit of the
// key, and O(n log n) compares and copies for driftsort.
template <class T, class Brand, std::size_t Extent, class ScratchBrand, std::size_t ScratchExtent,
          class Less = Ascending>
    requires SortElement<T> && StrictOrder<Less, T>
[[nodiscard]] constexpr Result<Unit, LengthMismatch> sort_stably(ViewMut<T, Brand, Extent> elements,
                                                                 ViewMut<T, ScratchBrand, ScratchExtent> scratch,
                                                                 Less less = Less{}) noexcept {
    std::size_t const count = elements.size();
    if (scratch.size() != count) return err(LengthMismatch{});
    View<T> const run = detail::run_of_(first_of_(detail::BaseLayerRead{}, elements), detail::SortCount{count});
    auto const room = side_as<side::SortScratch>(
        detail::run_of_(first_of_(detail::BaseLayerRead{}, scratch), detail::SortCount{count}));
    // The key of the radix sort has at most eight bytes.  A wider integer of a
    // GNU dialect, such as __int128, takes the comparator form.
    if constexpr (std::same_as<Less, Ascending> && sizeof(T) <= 8) {
        if (count >= stable_sort_radix_count) {
            if constexpr (std::same_as<T, float> || std::same_as<T, double>) {
                if !consteval {
                    // The key of a float is a bijection of its bits.  So at run
                    // time the sort writes the bits of the key of each element
                    // in its place one time, sorts the bits as an unsigned
                    // word, and writes each element back one time, in place of
                    // the key of each element in each pass.  A copy of a float
                    // keeps its bits.
                    detail::encode_radix_keys_(run);
                    detail::radix_sort_by_count_<sizeof(T)>(run, room, [](T const& element) noexcept {
                        return __builtin_bit_cast(detail::radix_key_t<T>, element);
                    });
                    detail::decode_radix_keys_(run);
                    return Unit{};
                }
            }
            detail::radix_sort_by_count_<sizeof(T)>(
                run, room, [](T const& element) noexcept { return detail::radix_key_(element); });
            return Unit{};
        }
    } else if constexpr (detail::is_radix_order_by_key_v<Less, T>) {
        if (count >= stable_sort_radix_count) {
            // The complement of the key of a descending order sorts it as an
            // ascending one, and two elements of one key keep their order.
            constexpr std::size_t bytes = sizeof(decltype(less.key_of(std::declval<T const&>())));
            detail::radix_sort_by_count_<bytes>(run, room, [&](T const& element) noexcept {
                auto const key = detail::radix_key_(less.key_of(element));
                if constexpr (Less::direction == KeyDirection::descending) {
                    return static_cast<decltype(key)>(~key);
                } else {
                    return key;
                }
            });
            return Unit{};
        }
    }
    detail::StableSort<T, Less> sorter{run, room, less};
    sorter.sort(run);
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
    std::remove_const_t<T> const* const first = first_of_(detail::BaseLayerRead{}, sorted);
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
    if (less(value, first_of_(detail::BaseLayerRead{}, sorted)[position])) return none;
    return Option<std::size_t>::some(position);
}

}  // namespace foundation::core
