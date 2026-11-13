// The differential test of foundation/core/Record.h against std::tuple and
// std::pair.
//
// A Record and a std::tuple of the same parts must give the same answer for
// each compare, and a Pack and a std::tuple of the same types must give the
// same size and the same type at each index.  A Record of empty parts must
// take no more bytes than the std forms.  Each std form stays in the one
// opt-out region of the class ORACLE, and each function of the region takes
// and gives plain values only.

#include <foundation/core/Record.h>

#include <foundation/Platform.h>
#include <foundation/Quarantine.h>

#include "philox_stream.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::Pack;
using ::foundation::core::pack_at_t;
using ::foundation::core::pack_size_v;
using ::foundation::core::Record;

struct TokenA final {};
struct TokenB final {};

// The order of two values as a number: -1, 0 or 1.
[[nodiscard]] int order_number(std::strong_ordering order) noexcept {
    return std::is_lt(order) ? -1 : (std::is_gt(order) ? 1 : 0);
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the differential test of Record and Pack against std::tuple and std::pair")

using TupleOfThree = std::tuple<std::uint32_t, std::uint8_t, std::int64_t>;

[[nodiscard]] bool tuple_equal(std::uint32_t first, std::uint8_t second, std::int64_t third, std::uint32_t other_first,
                               std::uint8_t other_second, std::int64_t other_third) noexcept {
    return TupleOfThree{first, second, third} == TupleOfThree{other_first, other_second, other_third};
}

[[nodiscard]] int tuple_order(std::uint32_t first, std::uint8_t second, std::int64_t third, std::uint32_t other_first,
                              std::uint8_t other_second, std::int64_t other_third) noexcept {
    return order_number(TupleOfThree{first, second, third} <=> TupleOfThree{other_first, other_second, other_third});
}

// The empty parts of different types collapse in a Record, and not in a
// std::pair.
static_assert(sizeof(Record<TokenA, TokenB>) <= sizeof(std::tuple<TokenA, TokenB>));
static_assert(sizeof(Record<TokenA, TokenB>) < sizeof(std::pair<TokenA, TokenB>));
static_assert(sizeof(Record<std::uint64_t, std::uint8_t>) == sizeof(std::pair<std::uint64_t, std::uint8_t>));

// A Pack and a std::tuple of the same types.
static_assert(pack_size_v<Pack<int, long, char>> == std::tuple_size_v<std::tuple<int, long, char>>);
static_assert(pack_size_v<Pack<>> == std::tuple_size_v<std::tuple<>>);
static_assert(
    std::is_same_v<pack_at_t<0, Pack<int, long, char>>, std::tuple_element_t<0, std::tuple<int, long, char>>>);
static_assert(
    std::is_same_v<pack_at_t<2, Pack<int, long, char>>, std::tuple_element_t<2, std::tuple<int, long, char>>>);

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The compares of a Record of three parts agree with the compares of the
// std::tuple of the same parts.  The values are small, so equal prefixes
// are frequent.  Linear in the count of trials.
void test_compares_agree_with_the_tuple() {
    ::crucible::test::PhiloxStream stream{0x5EEDC0DE0000B001u};
    for (int trial = 0; trial < 50000; ++trial) {
        auto const first = static_cast<std::uint32_t>(stream.below(3));
        auto const second = static_cast<std::uint8_t>(stream.below(3));
        auto const third = static_cast<std::int64_t>(stream.below(5)) - 2;
        auto const other_first = static_cast<std::uint32_t>(stream.below(3));
        auto const other_second = static_cast<std::uint8_t>(stream.below(3));
        auto const other_third = static_cast<std::int64_t>(stream.below(5)) - 2;
        Record<std::uint32_t, std::uint8_t, std::int64_t> const left{first, second, third};
        Record<std::uint32_t, std::uint8_t, std::int64_t> const right{other_first, other_second, other_third};
        bool const is_equal = left == right;
        CRUCIBLE_FATAL_INVARIANT(is_equal == tuple_equal(first, second, third, other_first, other_second, other_third));
        CRUCIBLE_FATAL_INVARIANT(order_number(left <=> right)
                                 == tuple_order(first, second, third, other_first, other_second, other_third));
    }
}

}  // namespace

int main() {
    test_compares_agree_with_the_tuple();
    return 0;
}
