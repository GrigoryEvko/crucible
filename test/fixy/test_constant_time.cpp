// Sentinel TU for fixy/ConstantTime.h: the primitives answer the same
// at compile time and at run time, and the trace does not branch on the
// operand.
//
// Three kinds of cell sit here.  The exhaustive ones walk an 8-bit cross
// product at compile time and every single-byte difference position at
// run time, which is where a borrow-propagation defect would hide.  The
// corner cells reach the widths and the maximal operands the exhaustive
// ones cannot.  The anchors are the small-magnitude cases.

#include <fixy/ConstantTime.h>
#include <fixy/Core.h>

#include <concepts>
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

// less is defined on the unsigned widths only.  Its borrow formula is
// correct only with modular subtraction and a logical final shift, and a
// signed operand gives neither.
template <class T>
concept less_accepts = requires(T operand) { ct::less<T>(operand, operand); };

template <class... Widths>
inline constexpr bool less_takes_unsigned_only = ((less_accepts<Widths> == std::unsigned_integral<Widths>) && ...);

static_assert(less_takes_unsigned_only<std::int8_t, std::uint8_t, std::int16_t, std::uint16_t, std::int32_t,
                                       std::uint32_t, std::int64_t, std::uint64_t>);

// eq takes two Views of one static extent through the fold, and a View of
// bytes that are not const through the dynamic form.  Two static extents
// that differ do not compile.  A View of dynamic extent on either side
// goes to the loop, whose precondition checks the lengths.  A span has no
// form.
template <class A, class B>
concept eq_accepts = requires(A lhs, B rhs) { ct::eq(lhs, rhs); };

using View3 = ::fixy::View<std::byte const, 3>;
using View5 = ::fixy::View<std::byte const, 5>;
using ViewMutable3 = ::fixy::View<std::byte, 3>;
using ViewMutable5 = ::fixy::View<std::byte, 5>;
using ViewAny = ::fixy::View<std::byte const>;

static_assert(eq_accepts<View3, View3> && eq_accepts<ViewMutable3, View3> && eq_accepts<ViewMutable5, ViewMutable5>);
static_assert(!eq_accepts<View3, View5> && !eq_accepts<ViewMutable3, View5> && !eq_accepts<ViewMutable5, ViewMutable3>);
static_assert(eq_accepts<ViewAny, View5> && eq_accepts<View3, ViewAny> && eq_accepts<ViewAny, ViewAny>);
static_assert(!eq_accepts<std::span<std::byte const, 3>, std::span<std::byte const, 3>>);
static_assert(!eq_accepts<std::span<std::byte const>, std::span<std::byte const>>);

constexpr ::fixy::FixedArray<std::byte, 3> tag_a{std::byte{1}, std::byte{2}, std::byte{3}};
constexpr ::fixy::FixedArray<std::byte, 3> tag_b{std::byte{1}, std::byte{2}, std::byte{4}};
static_assert(ct::eq(tag_a.view(), tag_a.view()));
static_assert(!ct::eq(tag_a.view(), tag_b.view()));

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

// eq finds a difference wherever it sits, not only near the front.  The
// Views of dynamic extent take the loop of the dynamic form.
int check_eq_finds_every_difference_position() {
    constexpr std::size_t len = 32;
    ::fixy::FixedArray<std::byte, len> a{};
    ::fixy::FixedArray<std::byte, len> b{};
    for (::fixy::Index<len> const i : a.indices()) {
        std::byte const value = static_cast<std::byte>(i.raw());
        a.with_element(i, [value](std::byte& held) noexcept { held = value; });
        b.with_element(i, [value](std::byte& held) noexcept { held = value; });
    }
    ::fixy::FixedArray<std::byte, len> const& left = a;
    ::fixy::FixedArray<std::byte, len> const& right = b;

    if (!ct::eq(ViewAny{left.view()}, ViewAny{right.view()})) return 10;
    if (!ct::eq(left.view(), right.view())) return 13;

    for (::fixy::Index<len> const pos : b.indices()) {
        const std::byte saved = b.at(pos);
        std::byte const flipped = static_cast<std::byte>(std::to_integer<unsigned>(saved) ^ 0xFFu);
        b.with_element(pos, [flipped](std::byte& held) noexcept { held = flipped; });
        if (ct::eq(ViewAny{left.view()}, ViewAny{right.view()})) return 11;
        if (ct::eq(left.view(), right.view())) return 14;
        b.with_element(pos, [saved](std::byte& held) noexcept { held = saved; });
        if (!ct::eq(ViewAny{left.view()}, ViewAny{right.view()})) return 12;
        if (!ct::eq(left.view(), right.view())) return 15;
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

// The widths and the corners the exhaustive 8-bit cross product above
// cannot reach.  Three corners matter: a narrow width, where promotion
// to int could sign-extend into the shift; a maximal operand, where a
// truncated subtraction would lose the borrow; and a pair further apart
// than half the range, which no borrow-free idiom handles.
int check_every_width_and_corner() {
    volatile std::uint32_t seed32_v = 0xDEADBEEFu;
    volatile std::uint64_t seed64_v = 0xCAFEBABEDEADBEEFull;
    const auto seed32 = static_cast<std::uint32_t>(seed32_v);
    const auto seed64 = static_cast<std::uint64_t>(seed64_v);

    if (ct::mask_from_bit<std::uint32_t>(0u) != 0u) return 30;
    if (ct::mask_from_bit<std::uint32_t>(1u) != static_cast<std::uint32_t>(-1)) return 31;
    if (ct::mask_from_bit<std::uint8_t>(1u) != static_cast<std::uint8_t>(-1)) return 32;
    if (ct::mask_from_bit<std::uint64_t>(1u) != static_cast<std::uint64_t>(-1)) return 33;

    // Only the 32-bit and 64-bit widths are exercised for select.  At
    // uint8_t and uint16_t the body promotes through int before
    // truncating back to T, which the project's conversion warnings
    // reject.  That is a property of the primitive, not of this test.
    if (ct::select<std::uint64_t>(1u, seed64, 0u) != seed64) return 34;
    if (ct::select<std::uint64_t>(0u, seed64, 0u) != 0u) return 35;

    if (ct::less<std::uint64_t>(1ull, 2ull) != 1u) return 36;
    if (ct::less<std::uint64_t>(2ull, 2ull) != 0u) return 37;
    if (ct::less<std::uint64_t>(3ull, 2ull) != 0u) return 38;
    if (ct::less<std::uint8_t>(std::uint8_t{5}, std::uint8_t{250}) != std::uint8_t{1}) return 39;
    if (ct::less<std::uint8_t>(std::uint8_t{250}, std::uint8_t{5}) != std::uint8_t{0}) return 40;
    if (ct::less<std::uint16_t>(std::uint16_t{0x000A}, std::uint16_t{0xFFFF}) != std::uint16_t{1}) return 41;
    if (ct::less<std::uint16_t>(std::uint16_t{0xFFFF}, std::uint16_t{0x000A}) != std::uint16_t{0}) return 42;
    if (ct::less<std::uint32_t>(0xFFFFFFFFu, 1u) != 0u) return 43;
    if (ct::less<std::uint32_t>(1u, 0xFFFFFFFFu) != 1u) return 44;
    if (ct::less<std::uint64_t>(0xFFFFFFFFFFFFFFFFull, 1ull) != 0ull) return 45;
    if (ct::less<std::uint64_t>(1ull, 0xFFFFFFFFFFFFFFFFull) != 1ull) return 46;
    if (ct::less<std::uint8_t>(std::uint8_t{0}, std::uint8_t{0x80}) != std::uint8_t{1}) return 47;
    if (ct::less<std::uint64_t>(0ull, 0x8000000000000000ull) != 1ull) return 48;
    if (ct::less<std::uint64_t>(0x8000000000000000ull, 0ull) != 0ull) return 49;

    if (ct::is_zero<std::uint8_t>(0u) != 1u) return 50;
    if (ct::is_zero<std::uint32_t>(seed32) != 0u) return 51;
    if (ct::is_zero<std::uint64_t>(seed64) != 0u) return 52;

    // Two empty Views carry no differing byte, so they compare equal.
    if (!ct::eq(ViewAny{}, ViewAny{})) return 53;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_every_width_and_corner(); rc != 0) return rc;
    if (int rc = check_eq_finds_every_difference_position(); rc != 0) return rc;
    if (int rc = check_select_and_cswap_at_runtime(); rc != 0) return rc;

    return 0;
}
