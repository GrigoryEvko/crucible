// The compile-time checks of foundation/core/Sort.h.

#include <foundation/core/Sort.h>

#include <foundation/core/Array.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace foundation::core {

namespace detail::sort_checks {

// The total order of IEEE 754: -NaN, -inf, the negative numbers, -0, +0, the
// positive numbers, +inf, +NaN.
inline constexpr float positive_nan = __builtin_bit_cast(float, 0x7FC00000u);
inline constexpr float negative_nan = __builtin_bit_cast(float, 0xFFC00000u);
inline constexpr float infinity = __builtin_bit_cast(float, 0x7F800000u);
static_assert(Ascending{}(negative_nan, -infinity) && Ascending{}(-infinity, -1.0f) && Ascending{}(-1.0f, -0.0f));
static_assert(Ascending{}(-0.0f, 0.0f) && Ascending{}(0.0f, 1.0f) && Ascending{}(1.0f, infinity));
static_assert(Ascending{}(infinity, positive_nan) && !Ascending{}(positive_nan, positive_nan));
static_assert(!Ascending{}(0.0f, -0.0f) && !Ascending{}(2.0f, 2.0f));
static_assert(Ascending{}(-0.0, 0.0) && Ascending{}(-1.0e300, -1.0e-300) && !Ascending{}(1.0, -1.0));
static_assert(Ascending{}(-3, 2) && !Ascending{}(4u, 4u));

// A record is not an element of Ascending, and an order that can throw is
// refused.
struct Record {
    std::uint32_t key = 0;
    std::uint32_t tag = 0;
};
struct ThrowingOrder {
    [[nodiscard]] bool operator()(int const& left, int const& right) const { return left < right; }
};
static_assert(StrictOrder<Ascending, int> && StrictOrder<Ascending, double>);
static_assert(!StrictOrder<Ascending, Record> && !StrictOrder<ThrowingOrder, int>);

// The network of four, and the counts of some networks.
constexpr auto network_of_four = plan_sort_network<4>();
static_assert(network_of_four.at(index<0>()) == Comparator{0, 1} && network_of_four.at(index<1>()) == Comparator{2, 3});
static_assert(network_of_four.at(index<2>()) == Comparator{0, 3} && network_of_four.at(index<3>()) == Comparator{1, 2});
static_assert(network_of_four.at(index<4>()) == Comparator{0, 1} && network_of_four.at(index<5>()) == Comparator{2, 3});
static_assert(sort_network_size<2> == 1 && sort_network_size<3> == 3 && sort_network_size<4> == 6);
static_assert(sort_network_size<8> == 24 && sort_network_size<16> == 80 && sort_network_size<256> == 4608);

// The network of N sorts each input of zeros and ones, so it sorts each
// input (the zero-one principle).
template <std::size_t N>
[[nodiscard]] consteval bool network_sorts_each_binary_input() noexcept {
    for (std::uint32_t input = 0; input < (std::uint32_t{1} << N); ++input) {
        FixedArray<std::uint8_t, N> wires{};
        std::size_t wire = 0;
        for (std::uint8_t& value : wires) {
            value = static_cast<std::uint8_t>((input >> wire) & 1u);
            ++wire;
        }
        wires = with_mut(std::move(wires), [](auto out) noexcept { sort_by_network(std::move(out)); });
        std::uint8_t last = 0;
        for (std::uint8_t const value : wires) {
            if (value < last) return false;
            last = value;
        }
    }
    return true;
}

template <std::size_t... N>
[[nodiscard]] consteval bool networks_sort(std::index_sequence<N...> /*counts*/) noexcept {
    return (network_sorts_each_binary_input<N + 2>() && ...);
}
static_assert(networks_sort(std::make_index_sequence<9>{}));

// The stable sort keeps two records of one key in their order of input.
[[nodiscard]] consteval bool sort_stably_keeps_equal_keys_in_order() noexcept {
    FixedArray<Record, 6> records{Record{3, 0}, Record{1, 1}, Record{3, 2}, Record{2, 3}, Record{1, 4}, Record{3, 5}};
    FixedArray<Record, 6> scratch{};
    auto const by_key = [](Record const& left, Record const& right) noexcept { return left.key < right.key; };
    bool is_sorted = false;
    records = with_mut(std::move(records), [&](auto elements) noexcept {
        scratch = with_mut(std::move(scratch), [&](auto room) noexcept {
            is_sorted = sort_stably(std::move(elements), std::move(room), by_key).is_ok();
        });
    });
    if (!is_sorted) return false;
    FixedArray<Record, 6> const expected{Record{1, 1}, Record{1, 4}, Record{2, 3},
                                         Record{3, 0}, Record{3, 2}, Record{3, 5}};
    for (Index<6> const position : records.indices()) {
        Record const& record = records.at(position);
        Record const& wanted = expected.at(position);
        if (record.key != wanted.key || record.tag != wanted.tag) return false;
    }
    return true;
}
static_assert(sort_stably_keeps_equal_keys_in_order());

// The comparator form of 200 records of seven keys, which takes the lazy merge
// tree, the stable quicksort, the partition of the keys equivalent to the
// pivot and the small sorts, keeps the records of one key in their order of
// input in constant evaluation too.
[[nodiscard]] consteval bool comparator_form_keeps_equal_keys_in_order() noexcept {
    FixedArray<Record, 200> records{};
    std::uint32_t tag = 0;
    std::uint32_t next = 5;
    for (Record& record : records) {
        next = (next * 37 + 11) % 101;
        record = Record{next % 7, tag};
        ++tag;
    }
    FixedArray<Record, 200> scratch{};
    auto const by_key = [](Record const& left, Record const& right) noexcept { return left.key < right.key; };
    bool is_sorted = false;
    records = with_mut(std::move(records), [&](auto elements) noexcept {
        scratch = with_mut(std::move(scratch), [&](auto room) noexcept {
            is_sorted = sort_stably(std::move(elements), std::move(room), by_key).is_ok();
        });
    });
    Record last{0, 0};
    bool is_first = true;
    for (Record const& record : records) {
        if (!is_first && (record.key < last.key || (record.key == last.key && record.tag < last.tag))) return false;
        last = record;
        is_first = false;
    }
    return is_sorted;
}
static_assert(comparator_form_keeps_equal_keys_in_order());

// A scratch of another count is refused.
[[nodiscard]] consteval bool short_scratch_is_refused() noexcept {
    FixedArray<int, 4> values{4, 3, 2, 1};
    FixedArray<int, 3> scratch{};
    bool is_refused = false;
    values = with_mut(std::move(values), [&](auto elements) noexcept {
        scratch = with_mut(std::move(scratch), [&](auto room) noexcept {
            is_refused = sort_stably(std::move(elements), std::move(room)).is_err();
        });
    });
    return is_refused;
}
static_assert(short_scratch_is_refused());

// The key of the radix sort orders as Ascending: the sign of an integer and
// of an enumeration of a signed base, and the total order of a float.
enum class SignedTag : std::int16_t {
};
enum class FlagTag : bool {
};
static_assert(radix_key_(-1) < radix_key_(0) && radix_key_(0) < radix_key_(1));
static_assert(radix_key_(std::int64_t{-1}) < radix_key_(std::int64_t{0}) && radix_key_(1u) < radix_key_(4000000000u));
static_assert(radix_key_(std::int8_t{-128}) < radix_key_(std::int8_t{127}));
static_assert(radix_key_(std::uint8_t{0}) < radix_key_(std::uint8_t{255}));
static_assert(radix_key_(SignedTag{-3}) < radix_key_(SignedTag{2})
              && radix_key_(FlagTag{false}) < radix_key_(FlagTag{true}));
static_assert(radix_key_(negative_nan) < radix_key_(-infinity) && radix_key_(-infinity) < radix_key_(-1.0f));
static_assert(radix_key_(-1.0f) < radix_key_(-0.0f) && radix_key_(-0.0f) < radix_key_(0.0f));
static_assert(radix_key_(1.0f) < radix_key_(infinity) && radix_key_(infinity) < radix_key_(positive_nan));
static_assert(radix_key_(-0.0) < radix_key_(0.0) && radix_key_(-1.0e300) < radix_key_(-1.0e-300));

// The radix sort of 300 values, which the count of stable_sort_radix_count
// selects, gives the order of Ascending in constant evaluation too.
[[nodiscard]] consteval bool radix_sort_orders_values() noexcept {
    static_assert(stable_sort_radix_count <= 300);
    FixedArray<int, 300> values{};
    int next = 7;
    for (int& value : values) {
        next = (next * 37 + 11) % 1009;
        value = next - 500;
    }
    FixedArray<int, 300> scratch{};
    bool is_sorted = false;
    values = with_mut(std::move(values), [&](auto elements) noexcept {
        scratch = with_mut(std::move(scratch), [&](auto room) noexcept {
            is_sorted = sort_stably(std::move(elements), std::move(room)).is_ok();
        });
    });
    int last = -501;
    for (int const value : values) {
        if (value < last) return false;
        last = value;
    }
    return is_sorted;
}
static_assert(radix_sort_orders_values());

// The searches of a sorted View.
constexpr FixedArray<int, 7> sorted{1, 3, 3, 3, 7, 9, 12};
static_assert(find_lower_bound(sorted.view(), 3) == 1 && find_lower_bound(sorted.view(), 4) == 4);
static_assert(find_lower_bound(sorted.view(), 0) == 0 && find_lower_bound(sorted.view(), 13) == 7);
static_assert(find_equivalent(sorted.view(), 3).expect("3 is in the run") == 1);
static_assert(find_equivalent(sorted.view(), 12).expect("12 is in the run") == 6);
static_assert(find_equivalent(sorted.view(), 8).is_none() && find_equivalent(sorted.view(), 13).is_none());
static_assert(find_lower_bound(View<int const>{}, 5) == 0 && find_equivalent(View<int const>{}, 5).is_none());

// A NaN has one place in a sorted View of floats.
constexpr FixedArray<float, 4> sorted_floats{-1.0f, 0.0f, 2.0f, positive_nan};
static_assert(find_equivalent(sorted_floats.view(), positive_nan).expect("the NaN is in the run") == 3);
static_assert(find_equivalent(sorted_floats.view(), -0.0f).is_none());

}  // namespace detail::sort_checks

}  // namespace foundation::core
