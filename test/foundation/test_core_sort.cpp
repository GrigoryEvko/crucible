// Tests of foundation/core/Sort.h: each network of up to 20 elements on each
// input of zeros and ones, the networks and the stable sort against a naive
// sort of the total order of IEEE 754, the stability of the stable sort, and
// the ordered searches against a scan.

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
    constexpr auto plan = ::foundation::core::sort_network_plan<N>();
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
               && ::foundation::core::total_order_key(moving)
                      < ::foundation::core::total_order_key(::crucible::test::element_at(values, position - 1))) {
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
        ::foundation::core::sort_network(values.view());
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
void test_stable_sort_is_stable() {
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
        assert(::foundation::core::stable_sort(records.view(), scratch.view(), by_key).is_ok());
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
void test_stable_sort_agrees_with_the_naive_sort() {
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
        assert(::foundation::core::stable_sort(values.view(), scratch.view()).is_ok());
        assert(same_bits(View<float const>{values.view()}, View<float const>{expected.view()}));
    }
}

// lower_bound and binary_search give the first position that a scan finds.
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
            assert(::foundation::core::lower_bound(sorted, probe) == first_not_below);
            bool const is_present =
                first_not_below < count && ::crucible::test::element_at(sorted, first_not_below) == probe;
            auto const found = ::foundation::core::binary_search(sorted, probe);
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
    test_stable_sort_is_stable();
    test_stable_sort_agrees_with_the_naive_sort();
    test_searches_agree_with_a_scan();
    crucible::test::pass("test_core_sort: all tests passed\n");
    return 0;
}
