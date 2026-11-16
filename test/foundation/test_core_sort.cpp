// Tests of foundation/core/Sort.h: each network of up to 20 elements on each
// input of zeros and ones, the networks and the stable sort against a naive
// sort of the total order of IEEE 754, the stability of the stable sort, the
// radix sort against the merge sort, and the ordered searches against a scan.

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
// come out in key order with the input order of each key.  The merges of runs
// in order take the copy, and the other merges compare.
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

// The radix sort of Ascending and the merge sort of the same order as a
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

void test_radix_sort_agrees_with_the_merge_sort() {
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
    test_radix_sort_agrees_with_the_merge_sort();
    test_searches_agree_with_a_scan();
    crucible::test::pass("test_core_sort: all tests passed\n");
    return 0;
}
