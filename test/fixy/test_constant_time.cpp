// Sentinel TU for fixy/ConstantTime.h: the primitives answer the same
// at compile time and at run time, and the trace does not branch on the
// operand.
//
// The header's self-test covers the anchors and the corners it reasons
// about.  This TU adds the property the header states but does not
// check: eq over every single-byte difference position, and less over an
// exhaustive 8-bit cross product, which is where a borrow-propagation
// defect would hide.

#include <fixy/ConstantTime.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace {

namespace ct = ::fixy::ct;

static_assert(ct::mask_from_bit<std::uint32_t>(0u) == 0u);
static_assert(ct::mask_from_bit<std::uint32_t>(1u) == 0xFFFFFFFFu);
static_assert(ct::is_zero<std::uint32_t>(0u) == 1u);
static_assert(ct::is_zero<std::uint32_t>(1u) == 0u);
static_assert(ct::less<std::uint32_t>(1u, 2u) == 1u);
static_assert(ct::less<std::uint32_t>(2u, 1u) == 0u);

// less agrees with the plain comparison at every 8-bit pair.  A same
// width subtraction discards the borrow, so this is the check that
// catches the idiom the header's comment warns against.
[[nodiscard]] consteval bool less_matches_ordering_exhaustively_u8() noexcept {
    for (unsigned a = 0; a < 256u; ++a) {
        for (unsigned b = 0; b < 256u; ++b) {
            const auto lhs = static_cast<std::uint8_t>(a);
            const auto rhs = static_cast<std::uint8_t>(b);
            const std::uint8_t want = (lhs < rhs) ? std::uint8_t{1} : std::uint8_t{0};
            if (ct::less<std::uint8_t>(lhs, rhs) != want) return false;
        }
    }
    return true;
}
static_assert(less_matches_ordering_exhaustively_u8());

// is_zero agrees at every 8-bit value.
[[nodiscard]] consteval bool is_zero_matches_exhaustively_u8() noexcept {
    for (unsigned v = 0; v < 256u; ++v) {
        const auto x = static_cast<std::uint8_t>(v);
        const std::uint8_t want = (x == 0) ? std::uint8_t{1} : std::uint8_t{0};
        if (ct::is_zero<std::uint8_t>(x) != want) return false;
    }
    return true;
}
static_assert(is_zero_matches_exhaustively_u8());

// eq finds a difference wherever it sits, not only near the front.
int check_eq_finds_every_difference_position() {
    constexpr std::size_t len = 32;
    std::byte a[len]{};
    std::byte b[len]{};
    for (std::size_t i = 0; i < len; ++i) {
        a[i] = static_cast<std::byte>(i);
        b[i] = static_cast<std::byte>(i);
    }

    if (!ct::eq(std::span<const std::byte>{a}, std::span<const std::byte>{b})) return 10;

    for (std::size_t pos = 0; pos < len; ++pos) {
        const std::byte saved = b[pos];
        b[pos] = static_cast<std::byte>(std::to_integer<unsigned>(saved) ^ 0xFFu);
        if (ct::eq(std::span<const std::byte>{a}, std::span<const std::byte>{b})) return 11;
        b[pos] = saved;
        if (!ct::eq(std::span<const std::byte>{a}, std::span<const std::byte>{b})) return 12;
    }
    return 0;
}

// select and cswap move the value only when the bit says so, with
// operands the compiler cannot fold.
int check_select_and_cswap_at_runtime() {
    volatile std::uint32_t seed = 0xDEADBEEFu;
    const auto x = static_cast<std::uint32_t>(seed);
    const std::uint32_t y = 0x12345678u;

    if (ct::select<std::uint32_t>(1u, x, y) != x) return 20;
    if (ct::select<std::uint32_t>(0u, x, y) != y) return 21;

    std::uint32_t a = x;
    std::uint32_t b = y;
    ct::cswap<std::uint32_t>(0u, a, b);
    if (a != x || b != y) return 22;
    ct::cswap<std::uint32_t>(1u, a, b);
    if (a != y || b != x) return 23;
    ct::cswap<std::uint32_t>(1u, a, b);
    if (a != x || b != y) return 24;

    return 0;
}

}  // namespace

int main() {
    ::fixy::ct::detail::ct_self_test::runtime_smoke_test();

    if (int rc = check_eq_finds_every_difference_position(); rc != 0) return rc;
    if (int rc = check_select_and_cswap_at_runtime(); rc != 0) return rc;

    return 0;
}
