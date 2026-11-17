// Tests of foundation/core/Sort.h: each network of up to 20 elements on each
// input of zeros and ones, the networks and the stable sort against a naive
// sort of the total order of IEEE 754, the stability of the stable sort, the
// paths of driftsort (few keys, runs of the input, the fallback of the
// quicksort under the adversary of McIlroy), the radix sort against the
// comparator form, and the ordered searches against a scan.

#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>

#include "../test_assert.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::HeapArray;
using ::foundation::core::View;

[[nodiscard]] ::foundation::effects::Alloc test_alloc() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// The wire words of a batch of 64 inputs of zeros and ones: bit l of wire w
// is bit w of the input base + l.
[[nodiscard]] std::uint64_t wire_word(std::size_t wire, std::uint64_t base) noexcept {
    ::fixy::FixedArray<std::uint64_t, 6> const low_wires{0xAAAAAAAAAAAAAAAAu, 0xCCCCCCCCCCCCCCCCu, 0xF0F0F0F0F0F0F0F0u,
                                                         0xFF00FF00FF00FF00u, 0xFFFF0000FFFF0000u, 0xFFFFFFFF00000000u};
    if (wire < 6) return ::crucible::test::element_at(low_wires, wire);
    return ((base >> wire) & 1u) != 0 ? ~std::uint64_t{0} : std::uint64_t{0};
}

// The network of N sorts each of the 2^N inputs of zeros and ones.  Each
// wire holds the bits of 64 inputs, so a comparator is an and and an or.
template <std::size_t N>
[[nodiscard]] bool network_sorts_each_binary_input() noexcept {
    constexpr auto plan = ::foundation::core::plan_sort_network<N>();
    std::uint64_t const inputs = std::uint64_t{1} << N;
    std::uint64_t const lanes = inputs < 64 ? ((std::uint64_t{1} << inputs) - 1) : ~std::uint64_t{0};
    ::fixy::FixedArray<std::uint64_t, N> wires{};
    View<std::uint64_t> const wire_view = wires.view();
    for (std::uint64_t base = 0; base < inputs; base += 64) {
        for (std::size_t wire = 0; wire < N; ++wire) {
            ::crucible::test::element_at(wire_view, wire) = wire_word(wire, base);
        }
        for (::foundation::core::Comparator const step : plan) {
            std::uint64_t& low = ::crucible::test::element_at(wire_view, step.low);
            std::uint64_t& high = ::crucible::test::element_at(wire_view, step.high);
            std::uint64_t const minimum = low & high;
            high = low | high;
            low = minimum;
        }
        for (std::size_t wire = 0; wire + 1 < N; ++wire) {
            std::uint64_t const below = ::crucible::test::element_at(wire_view, wire);
            std::uint64_t const above = ::crucible::test::element_at(wire_view, wire + 1);
            if ((below & ~above & lanes) != 0) return false;
        }
    }
    return true;
}

template <std::size_t... N>
[[nodiscard]] bool networks_sort(std::index_sequence<N...> /*counts*/) noexcept {
    return (network_sorts_each_binary_input<N + 2>() && ...);
}

void test_networks_sort_each_binary_input() { assert(networks_sort(std::make_index_sequence<19>{})); }

// A float of each class: a NaN of each sign and some payloads, an infinity,
// a zero of each sign, a subnormal, and a normal number.
[[nodiscard]] float draw_special_float(::crucible::test::PhiloxStream& stream) noexcept {
    std::uint32_t const word = stream.next_word();
    std::uint32_t const sign = word & 0x80000000u;
    switch (stream.below(6)) {
        case 0:
            return ::fixy::bit_cast<float>(sign | 0x7FC00000u | (word & 0x3Fu));
        case 1:
            return ::fixy::bit_cast<float>(sign | 0x7F800000u);
        case 2:
            return ::fixy::bit_cast<float>(sign);
        case 3:
            return ::fixy::bit_cast<float>(sign | (word & 0x7Fu));
        default:
            return ::fixy::bit_cast<float>(sign | (0x3F800000u + (word & 0x00FFFFFFu)));
    }
}

[[nodiscard]] std::uint32_t bits_of(float value) noexcept { return ::fixy::bit_cast<std::uint32_t>(value); }

// The naive sort: an insertion sort of the total order, by its key.
void naive_sort(View<float> values) noexcept {
    for (std::size_t index = 1; index < values.size(); ++index) {
        float const moving = ::crucible::test::element_at(values, index);
        std::size_t position = index;
        while (position > 0
               && ::foundation::core::encode_total_order_key(moving) < ::foundation::core::encode_total_order_key(
                      ::crucible::test::element_at(values, position - 1))) {
            ::crucible::test::element_at(values, position) = ::crucible::test::element_at(values, position - 1);
            --position;
        }
        ::crucible::test::element_at(values, position) = moving;
    }
}

[[nodiscard]] bool same_bits(View<float const> left, View<float const> right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (bits_of(::crucible::test::element_at(left, index)) != bits_of(::crucible::test::element_at(right, index))) {
            return false;
        }
    }
    return true;
}

// The network of N gives the bits of the naive sort, for floats of each
// class.
template <std::size_t N>
void check_network_on_floats(::crucible::test::PhiloxStream& stream) noexcept {
    for (int trial = 0; trial < 64; ++trial) {
        ::fixy::FixedArray<float, N> values{};
        for (float& value : values) {
            value = draw_special_float(stream);
        }
        ::fixy::FixedArray<float, N> expected{values};
        naive_sort(View<float>{expected.view()});
        values = ::foundation::core::with_mut(
            std::move(values), [](auto out) noexcept { ::foundation::core::sort_by_network(std::move(out)); });
        assert(same_bits(View<float const>{values.view()}, View<float const>{expected.view()}));
    }
}

void test_networks_agree_with_the_naive_sort() {
    ::crucible::test::PhiloxStream stream{0x50470001u};
    check_network_on_floats<2>(stream);
    check_network_on_floats<3>(stream);
    check_network_on_floats<5>(stream);
    check_network_on_floats<8>(stream);
    check_network_on_floats<13>(stream);
    check_network_on_floats<16>(stream);
    check_network_on_floats<31>(stream);
    check_network_on_floats<32>(stream);
    check_network_on_floats<33>(stream);
    check_network_on_floats<64>(stream);
    check_network_on_floats<100>(stream);
    check_network_on_floats<256>(stream);
}

// The stable sort of owner with the scratch of scratch, through a frame of
// each.  True when the sort gave no error.
template <class Owner, class... Less>
[[nodiscard]] bool sort_through_frames(Owner& owner, Owner& scratch, Less... less) noexcept {
    bool is_sorted = false;
    owner = ::foundation::core::with_mut(std::move(owner), [&](auto elements) noexcept {
        scratch = ::foundation::core::with_mut(std::move(scratch), [&](auto room) noexcept {
            is_sorted = ::foundation::core::sort_stably(std::move(elements), std::move(room), less...).is_ok();
        });
    });
    return is_sorted;
}

// A record with a key that many records share, and the position of the
// record in the input.
struct Keyed {
    std::uint32_t key = 0;
    std::uint32_t position = 0;
};

[[nodiscard]] bool is_below(Keyed const& left, Keyed const& right) noexcept { return left.key < right.key; }

// The stable sort orders the records by key, keeps the input order of the
// records of one key, and keeps each record, for each count to 300 and the
// counts around each power of two to 2^16.
void test_sort_stably_is_stable() {
    ::crucible::test::PhiloxStream stream{0x50470002u};
    auto const by_key = [](Keyed const& left, Keyed const& right) noexcept { return is_below(left, right); };
    auto const check_count = [&](std::size_t count) noexcept {
        HeapArray<Keyed> records = ::foundation::core::mint_heap_array(test_alloc(), count, Keyed{});
        HeapArray<Keyed> scratch = ::foundation::core::mint_heap_array(test_alloc(), count, Keyed{});
        std::uint32_t position = 0;
        for (Keyed& record : records) {
            record = Keyed{static_cast<std::uint32_t>(stream.below(1 + count / 4)), position};
            ++position;
        }
        assert(sort_through_frames(records, scratch, by_key));
        HeapArray<std::uint8_t> seen = ::foundation::core::mint_heap_array(test_alloc(), count, std::uint8_t{0});
        View<Keyed> const sorted = records.view();
        for (std::size_t index = 0; index < count; ++index) {
            Keyed const record = ::crucible::test::element_at(sorted, index);
            std::uint8_t& mark = ::crucible::test::element_at(seen.view(), record.position);
            assert(mark == 0);
            mark = 1;
            if (index == 0) continue;
            Keyed const before = ::crucible::test::element_at(sorted, index - 1);
            assert(before.key < record.key || (before.key == record.key && before.position < record.position));
        }
    };
    for (std::size_t count = 0; count <= 300; ++count) {
        check_count(count);
    }
    for (std::size_t power = 512; power <= std::size_t{1} << 16; power *= 2) {
        check_count(power - 1);
        check_count(power);
        check_count(power + 1);
    }
}

// The stable sort of floats of each class gives the bits of the naive sort.
void test_sort_stably_agrees_with_the_naive_sort() {
    ::crucible::test::PhiloxStream stream{0x50470003u};
    ::fixy::FixedArray<std::size_t, 8> const counts{0, 1, 2, 31, 32, 33, 100, 1500};
    for (std::size_t const count : counts) {
        HeapArray<float> values = ::foundation::core::mint_heap_array(test_alloc(), count, 0.0f);
        for (float& value : values) {
            value = draw_special_float(stream);
        }
        HeapArray<float> expected =
            ::foundation::core::mint_heap_array_copy<float>(test_alloc(), View<float const>{values.view()});
        naive_sort(expected.view());
        HeapArray<float> scratch = ::foundation::core::mint_heap_array(test_alloc(), count, 0.0f);
        assert(sort_through_frames(values, scratch));
        assert(same_bits(View<float const>{values.view()}, View<float const>{expected.view()}));
    }
}

// Records in key order stay in place, and records in the reverse key order
// come out in key order with the input order of each key.  An input in key
// order is one sorted run of the merge tree.  The reverse order with ties has
// no strictly descending run longer than one key, so it takes the small sorts
// and the quicksort.
void test_sort_stably_of_ordered_runs() {
    auto const by_key = [](Keyed const& left, Keyed const& right) noexcept { return is_below(left, right); };
    ::fixy::FixedArray<std::size_t, 9> const counts{1, 31, 32, 33, 64, 65, 300, 1000, 4097};
    for (std::size_t const count : counts) {
        HeapArray<Keyed> records = ::foundation::core::mint_heap_array(test_alloc(), count, Keyed{});
        HeapArray<Keyed> scratch = ::foundation::core::mint_heap_array(test_alloc(), count, Keyed{});
        std::uint32_t position = 0;
        for (Keyed& record : records) {
            record = Keyed{position / 3, position};
            ++position;
        }
        assert(sort_through_frames(records, scratch, by_key));
        position = 0;
        for (Keyed const record : records) {
            assert(record.key == position / 3 && record.position == position);
            ++position;
        }
        auto const last = static_cast<std::uint32_t>(count - 1);
        position = 0;
        for (Keyed& record : records) {
            record = Keyed{(last - position) / 3, position};
            ++position;
        }
        assert(sort_through_frames(records, scratch, by_key));
        View<Keyed const> const sorted{records.view()};
        for (std::size_t index = 1; index < count; ++index) {
            Keyed const before = ::crucible::test::element_at(sorted, index - 1);
            Keyed const record = ::crucible::test::element_at(sorted, index);
            assert(before.key < record.key || (before.key == record.key && before.position < record.position));
        }
    }
}

// True when the records are in key order, the records of one key are in
// their input order, and each position of the input is there once.
[[nodiscard]] bool is_stable_order(View<Keyed const> sorted) noexcept {
    HeapArray<std::uint8_t> seen = ::foundation::core::mint_heap_array(test_alloc(), sorted.size(), std::uint8_t{0});
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        Keyed const record = ::crucible::test::element_at(sorted, index);
        if (record.position >= sorted.size()) return false;
        std::uint8_t& mark = ::crucible::test::element_at(seen.view(), record.position);
        if (mark != 0) return false;
        mark = 1;
        if (index == 0) continue;
        Keyed const before = ::crucible::test::element_at(sorted, index - 1);
        if (before.key > record.key || (before.key == record.key && before.position > record.position)) return false;
    }
    return true;
}

// The records of count positions with the key of each position from a rule.
template <class KeyRule>
[[nodiscard]] HeapArray<Keyed> draw_records(std::size_t count, KeyRule rule) noexcept {
    HeapArray<Keyed> records = ::foundation::core::mint_heap_array(test_alloc(), count, Keyed{});
    std::uint32_t position = 0;
    for (Keyed& record : records) {
        record = Keyed{rule(position), position};
        ++position;
    }
    return records;
}

// The stable sort of the records by key, and whether the result is in stable
// order.
[[nodiscard]] bool sorts_stably(HeapArray<Keyed>& records) noexcept {
    auto const by_key = [](Keyed const& left, Keyed const& right) noexcept { return is_below(left, right); };
    HeapArray<Keyed> scratch = ::foundation::core::mint_heap_array(test_alloc(), records.size(), Keyed{});
    return sort_through_frames(records, scratch, by_key) && is_stable_order(View<Keyed const>{records.view()});
}

// Records of two or four keys take the partition of the elements equivalent
// to the pivot, in the part of the quicksort above the small sorts.
void test_sort_stably_of_few_keys() {
    ::crucible::test::PhiloxStream stream{0x50470006u};
    ::fixy::FixedArray<std::size_t, 3> const counts{65536, 100003, 262147};
    for (std::size_t const count : counts) {
        HeapArray<Keyed> two =
            draw_records(count, [&](std::uint32_t) noexcept { return static_cast<std::uint32_t>(stream.below(2)); });
        assert(sorts_stably(two));
        HeapArray<Keyed> four =
            draw_records(count, [&](std::uint32_t) noexcept { return static_cast<std::uint32_t>(stream.below(4)); });
        assert(sorts_stably(four));
        HeapArray<Keyed> one = draw_records(count, [](std::uint32_t) noexcept { return std::uint32_t{7}; });
        assert(sorts_stably(one));
    }
}

// The runs of the input: one strictly descending run, which the merge tree
// reverses, runs of 500 records in alternate orders, which it merges, a saw of
// short runs, and an input in order with one record out of order.
void test_sort_stably_of_input_runs() {
    ::fixy::FixedArray<std::size_t, 4> const counts{4095, 4097, 65536, 100003};
    for (std::size_t const count : counts) {
        auto const last = static_cast<std::uint32_t>(count);
        HeapArray<Keyed> descending =
            draw_records(count, [&](std::uint32_t position) noexcept { return last - position; });
        assert(sorts_stably(descending));
        HeapArray<Keyed> alternate = draw_records(count, [&](std::uint32_t position) noexcept {
            return (position / 500) % 2 == 0 ? position : last - position;
        });
        assert(sorts_stably(alternate));
        HeapArray<Keyed> saw = draw_records(count, [](std::uint32_t position) noexcept { return position % 97; });
        assert(sorts_stably(saw));
        HeapArray<Keyed> almost = draw_records(
            count, [&](std::uint32_t position) noexcept { return position == last / 2 ? std::uint32_t{0} : position; });
        assert(sorts_stably(almost));
    }
}

// The adversary of McIlroy (A Killer Adversary for Quicksort, 1999).  A gas
// key is above each solid key.  A compare of two gas keys first gives one of
// them the next solid key, so each answer agrees with the final keys.  The
// candidate is the gas key of the last compare that held one, and a compare of
// the candidate with a gas key freezes the candidate.  A quicksort with a
// pivot from a few samples then meets a pivot near an end of each run.
//
// The scan of the runs of driftsort compares neighbors, and that rule then
// gives the keys in order, one sorted run.  So the keys of the odd positions
// start solid: each run of the scan then ends after two elements, the input
// is one unsorted run, and the gas keys of the even positions, above each
// solid key, meet the adversary in the quicksort.
class QuicksortAdversary {
public:
    explicit QuicksortAdversary(View<std::uint32_t> keys) noexcept : keys_{keys}, gas_{keys.size()} {
        for (std::size_t index = 0; index < keys.size(); ++index) {
            ::crucible::test::element_at(keys_, index) = index % 2 == 0 ? static_cast<std::uint32_t>(gas_) : solid_++;
        }
    }

    [[nodiscard]] bool is_below(Keyed const& left, Keyed const& right) noexcept {
        std::uint32_t& left_key = ::crucible::test::element_at(keys_, left.position);
        std::uint32_t& right_key = ::crucible::test::element_at(keys_, right.position);
        if (left_key == gas_ && right_key == gas_) {
            if (left.position == candidate_) {
                left_key = solid_++;
            } else {
                right_key = solid_++;
            }
        }
        if (left_key == gas_) {
            candidate_ = left.position;
        } else if (right_key == gas_) {
            candidate_ = right.position;
        }
        return left_key < right_key;
    }

    // Gives each key that is still gas the next solid key, in the order of
    // the positions.
    void freeze_each() noexcept {
        for (std::size_t index = 0; index < keys_.size(); ++index) {
            std::uint32_t& key = ::crucible::test::element_at(keys_, index);
            if (key == gas_) key = solid_++;
        }
    }

private:
    View<std::uint32_t> keys_;
    std::size_t gas_ = 0;
    std::uint32_t solid_ = 0;
    std::uint32_t candidate_ = 0;
};

// The keys of the adversary make an input on which the quicksort meets a
// pivot near an end of each run, so it spends the budget of each path, and
// the eager merge tree sorts the rest.  The sort of that input stays stable
// and makes at most 3 n log2 n compares.  Each pivot of the adversary takes
// about 28 samples, so a quicksort with no budget makes about n^2 / 56
// compares: 5.1 n log2 n for 2^14 records, and about four times more for
// 2^16.  With the budget, 2^16 records take 1.53 n log2 n.
void test_sort_stably_bounds_the_adversary() {
    constexpr std::size_t count = std::size_t{1} << 16;
    HeapArray<std::uint32_t> keys = ::foundation::core::mint_heap_array(test_alloc(), count, std::uint32_t{0});
    QuicksortAdversary adversary{keys.view()};
    HeapArray<Keyed> probe = draw_records(count, [](std::uint32_t) noexcept { return std::uint32_t{0}; });
    HeapArray<Keyed> probe_scratch = ::foundation::core::mint_heap_array(test_alloc(), count, Keyed{});
    auto const by_adversary = [&](Keyed const& left, Keyed const& right) noexcept {
        return adversary.is_below(left, right);
    };
    assert(sort_through_frames(probe, probe_scratch, by_adversary));
    adversary.freeze_each();
    View<std::uint32_t const> const frozen{keys.view()};
    HeapArray<Keyed> records = draw_records(
        count, [&](std::uint32_t position) noexcept { return ::crucible::test::element_at(frozen, position); });
    std::size_t compares = 0;
    auto const by_counted_key = [&](Keyed const& left, Keyed const& right) noexcept {
        ++compares;
        return left.key < right.key;
    };
    HeapArray<Keyed> scratch = ::foundation::core::mint_heap_array(test_alloc(), count, Keyed{});
    assert(sort_through_frames(records, scratch, by_counted_key));
    assert(is_stable_order(View<Keyed const>{records.view()}));
    assert(compares <= 3 * count * 16);
}

// An enumeration of a signed base, and one of an unsigned base, for the keys
// of the radix sort.
enum class SignedTag : std::int16_t {
};
enum class ByteTag : std::uint8_t {
};

// The element of a word: the low bytes of the word, as T.
template <class T>
[[nodiscard]] T element_of_word(std::uint64_t word) noexcept {
    if constexpr (sizeof(T) == 1) {
        return ::fixy::bit_cast<T>(static_cast<std::uint8_t>(word));
    } else if constexpr (sizeof(T) == 2) {
        return ::fixy::bit_cast<T>(static_cast<std::uint16_t>(word));
    } else if constexpr (sizeof(T) == 4) {
        return ::fixy::bit_cast<T>(static_cast<std::uint32_t>(word));
    } else {
        return ::fixy::bit_cast<T>(word);
    }
}

// The bits of an element, in a word.
template <class T>
[[nodiscard]] std::uint64_t word_of_element(T value) noexcept {
    if constexpr (sizeof(T) == 1) {
        return ::fixy::bit_cast<std::uint8_t>(value);
    } else if constexpr (sizeof(T) == 2) {
        return ::fixy::bit_cast<std::uint16_t>(value);
    } else if constexpr (sizeof(T) == 4) {
        return ::fixy::bit_cast<std::uint32_t>(value);
    } else {
        return ::fixy::bit_cast<std::uint64_t>(value);
    }
}

// An element of T with many ties: a small value, a value with each high bit
// set, or a random word.  A float is a float of each class, and a double is
// a float of each class or a random word.
template <class T>
[[nodiscard]] T draw_element(::crucible::test::PhiloxStream& stream) noexcept {
    if constexpr (std::same_as<T, float>) {
        return draw_special_float(stream);
    } else {
        std::uint64_t const word = stream.next_wide();
        if constexpr (std::same_as<T, double>) {
            if (word % 2 == 0) return static_cast<double>(draw_special_float(stream));
            return ::fixy::bit_cast<double>(stream.next_wide());
        } else {
            switch (word % 3) {
                case 0:
                    return element_of_word<T>(word >> 60);
                case 1:
                    return element_of_word<T>(~(word >> 60));
                default:
                    return element_of_word<T>(stream.next_wide());
            }
        }
    }
}

// The radix sort of Ascending and the comparator form of the same order as a
// function of the caller give the same bits, for counts below and above
// stable_sort_radix_count.
template <class T>
void check_radix_against_merge(::crucible::test::PhiloxStream& stream) noexcept {
    auto const by_order = [](T const& left, T const& right) noexcept {
        return ::foundation::core::Ascending{}(left, right);
    };
    ::fixy::FixedArray<std::size_t, 8> const counts{1, 255, 256, 257, 300, 1000, 4097, 20000};
    for (std::size_t const count : counts) {
        HeapArray<T> radix = ::foundation::core::mint_heap_array(test_alloc(), count, T{});
        for (T& value : radix) {
            value = draw_element<T>(stream);
        }
        HeapArray<T> merged = ::foundation::core::mint_heap_array_copy<T>(test_alloc(), View<T const>{radix.view()});
        HeapArray<T> scratch = ::foundation::core::mint_heap_array(test_alloc(), count, T{});
        assert(sort_through_frames(radix, scratch));
        assert(sort_through_frames(merged, scratch, by_order));
        View<T const> const radix_view{radix.view()};
        View<T const> const merged_view{merged.view()};
        for (std::size_t index = 0; index < count; ++index) {
            T const value = ::crucible::test::element_at(radix_view, index);
            assert(word_of_element(value) == word_of_element(::crucible::test::element_at(merged_view, index)));
            if (index > 0) assert(!by_order(value, ::crucible::test::element_at(radix_view, index - 1)));
        }
    }
}

void test_radix_sort_agrees_with_the_comparator_form() {
    ::crucible::test::PhiloxStream stream{0x50470005u};
    check_radix_against_merge<std::uint8_t>(stream);
    check_radix_against_merge<std::int8_t>(stream);
    check_radix_against_merge<char>(stream);
    check_radix_against_merge<std::uint16_t>(stream);
    check_radix_against_merge<std::int16_t>(stream);
    check_radix_against_merge<std::uint32_t>(stream);
    check_radix_against_merge<std::int32_t>(stream);
    check_radix_against_merge<std::uint64_t>(stream);
    check_radix_against_merge<std::int64_t>(stream);
    check_radix_against_merge<SignedTag>(stream);
    check_radix_against_merge<ByteTag>(stream);
    check_radix_against_merge<float>(stream);
    check_radix_against_merge<double>(stream);
}

// find_lower_bound and find_equivalent give the first position that a scan finds.
void test_searches_agree_with_a_scan() {
    ::crucible::test::PhiloxStream stream{0x50470004u};
    for (int trial = 0; trial < 400; ++trial) {
        std::size_t const count = stream.below(80);
        HeapArray<std::int32_t> values = ::foundation::core::mint_heap_array(test_alloc(), count, std::int32_t{0});
        std::int32_t next = -20;
        for (std::int32_t& value : values) {
            next += static_cast<std::int32_t>(stream.below(3));
            value = next;
        }
        View<std::int32_t const> const sorted{values.view()};
        for (std::int32_t probe = -22; probe <= next + 2; ++probe) {
            std::size_t first_not_below = count;
            for (std::size_t index = 0; index < count; ++index) {
                if (::crucible::test::element_at(sorted, index) >= probe) {
                    first_not_below = index;
                    break;
                }
            }
            assert(::foundation::core::find_lower_bound(sorted, probe) == first_not_below);
            bool const is_present =
                first_not_below < count && ::crucible::test::element_at(sorted, first_not_below) == probe;
            auto const found = ::foundation::core::find_equivalent(sorted, probe);
            assert(found.is_some() == is_present);
            for (std::size_t const position : found) {
                assert(position == first_not_below);
            }
        }
    }
}

}  // namespace

int main() {
    test_networks_sort_each_binary_input();
    test_networks_agree_with_the_naive_sort();
    test_sort_stably_is_stable();
    test_sort_stably_agrees_with_the_naive_sort();
    test_sort_stably_of_ordered_runs();
    test_sort_stably_of_few_keys();
    test_sort_stably_of_input_runs();
    test_sort_stably_bounds_the_adversary();
    test_radix_sort_agrees_with_the_comparator_form();
    test_searches_agree_with_a_scan();
    crucible::test::pass("test_core_sort: all tests passed\n");
    return 0;
}
