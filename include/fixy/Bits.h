#pragma once

// A flag word whose bits are named by one scoped enum.  Two Bits over
// different enums are different types and do not compose, which is what
// stops a flag from one enum being written into another's word.
//
// The wrapper does not know which flags exclude each other.  A pair of
// mutually exclusive flags can still be set together, and enforcing
// that would need a per-enum statement of which sets are exclusive.
//
// Old spelling: include/crucible/safety/Bits.h.

#include <foundation/Platform.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string_view>
#include <type_traits>

namespace fixy {

// An unscoped enum converts to its underlying integer on its own, which
// would let a raw integer through the typed surface unannounced.

template <class E>
concept ScopedEnum = std::is_scoped_enum_v<E>;

template <ScopedEnum EnumType>
class [[nodiscard]] Bits {
public:
    using enum_type = EnumType;
    using underlying_type = std::underlying_type_t<EnumType>;

    static constexpr std::string_view wrapper_kind() noexcept { return "structural::Bits"; }

private:
    underlying_type bits_{0};

    // The tag keeps the raw constructor out of brace initialization, so
    // `Bits<E>{42}` does not compile and every raw entry has to spell the
    // named factory.
    struct from_raw_tag_t {};
    constexpr Bits(from_raw_tag_t, underlying_type b) noexcept : bits_{b} {}

    [[nodiscard]] static constexpr underlying_type to_underlying(EnumType e) noexcept {
        return static_cast<underlying_type>(e);
    }

public:
    constexpr Bits() noexcept = default;

    constexpr Bits(std::initializer_list<EnumType> flags) noexcept {
        underlying_type acc = 0;
        for (EnumType f : flags) {
            acc = static_cast<underlying_type>(acc | to_underlying(f));
        }
        bits_ = acc;
    }

    [[nodiscard]] static constexpr Bits from_raw(underlying_type b) noexcept { return Bits{from_raw_tag_t{}, b}; }

    constexpr Bits(Bits const&) = default;
    constexpr Bits(Bits&&) = default;
    constexpr Bits& operator=(Bits const&) = default;
    constexpr Bits& operator=(Bits&&) = default;
    ~Bits() = default;

    constexpr void set(EnumType f) noexcept { bits_ = static_cast<underlying_type>(bits_ | to_underlying(f)); }
    constexpr void unset(EnumType f) noexcept {
        bits_ = static_cast<underlying_type>(bits_ & static_cast<underlying_type>(~to_underlying(f)));
    }
    constexpr void toggle(EnumType f) noexcept { bits_ = static_cast<underlying_type>(bits_ ^ to_underlying(f)); }
    constexpr void clear() noexcept { bits_ = 0; }

    [[nodiscard]] constexpr bool test(EnumType f) const noexcept {
        return (bits_ & to_underlying(f)) != underlying_type{0};
    }
    [[nodiscard]] constexpr bool none() const noexcept { return bits_ == underlying_type{0}; }
    [[nodiscard]] constexpr bool any() const noexcept { return bits_ != underlying_type{0}; }
    [[nodiscard]] constexpr int popcount() const noexcept {
        return std::popcount(static_cast<std::make_unsigned_t<underlying_type>>(bits_));
    }

    [[nodiscard]] constexpr underlying_type raw() const noexcept { return bits_; }

    [[nodiscard]] friend constexpr bool operator==(Bits, Bits) noexcept = default;

    // These are hidden friends rather than free templates.  Each
    // instantiation gets its own overload set, so an operand pair drawn
    // from two different enums finds no candidate at all.
    [[nodiscard]] friend constexpr Bits operator|(Bits a, Bits b) noexcept {
        return Bits{from_raw_tag_t{}, static_cast<underlying_type>(a.bits_ | b.bits_)};
    }
    [[nodiscard]] friend constexpr Bits operator&(Bits a, Bits b) noexcept {
        return Bits{from_raw_tag_t{}, static_cast<underlying_type>(a.bits_ & b.bits_)};
    }
    [[nodiscard]] friend constexpr Bits operator^(Bits a, Bits b) noexcept {
        return Bits{from_raw_tag_t{}, static_cast<underlying_type>(a.bits_ ^ b.bits_)};
    }
    [[nodiscard]] friend constexpr Bits operator~(Bits a) noexcept {
        return Bits{from_raw_tag_t{}, static_cast<underlying_type>(~a.bits_)};
    }

    [[nodiscard]] friend constexpr Bits operator|(Bits a, EnumType e) noexcept {
        return Bits{from_raw_tag_t{}, static_cast<underlying_type>(a.bits_ | to_underlying(e))};
    }
    [[nodiscard]] friend constexpr Bits operator|(EnumType e, Bits b) noexcept { return b | e; }
    [[nodiscard]] friend constexpr Bits operator&(Bits a, EnumType e) noexcept {
        return Bits{from_raw_tag_t{}, static_cast<underlying_type>(a.bits_ & to_underlying(e))};
    }

    constexpr Bits& operator|=(Bits other) noexcept {
        bits_ = static_cast<underlying_type>(bits_ | other.bits_);
        return *this;
    }
    constexpr Bits& operator|=(EnumType e) noexcept {
        bits_ = static_cast<underlying_type>(bits_ | to_underlying(e));
        return *this;
    }
    constexpr Bits& operator&=(Bits other) noexcept {
        bits_ = static_cast<underlying_type>(bits_ & other.bits_);
        return *this;
    }
    constexpr Bits& operator^=(Bits other) noexcept {
        bits_ = static_cast<underlying_type>(bits_ ^ other.bits_);
        return *this;
    }
};

namespace detail::bits_layout {

enum class TestU8 : std::uint8_t {
    A = 0x01,
    B = 0x02,
    C = 0x04,
    D = 0x08
};
enum class TestU16 : std::uint16_t {
    X = 0x0001,
    Y = 0x0100
};
enum class TestU32 : std::uint32_t {
    K = 0x00010000U,
    L = 0x80000000U
};
enum class TestU64 : std::uint64_t {
    M = 0x0000000100000000ULL
};

}  // namespace detail::bits_layout

static_assert(sizeof(Bits<detail::bits_layout::TestU8>) == sizeof(std::uint8_t));
static_assert(sizeof(Bits<detail::bits_layout::TestU16>) == sizeof(std::uint16_t));
static_assert(sizeof(Bits<detail::bits_layout::TestU32>) == sizeof(std::uint32_t));
static_assert(sizeof(Bits<detail::bits_layout::TestU64>) == sizeof(std::uint64_t));

static_assert(std::is_trivially_copyable_v<Bits<detail::bits_layout::TestU8>>);
static_assert(std::is_trivially_destructible_v<Bits<detail::bits_layout::TestU8>>);
static_assert(std::is_standard_layout_v<Bits<detail::bits_layout::TestU8>>);

namespace detail::bits_self_test {

using F = ::fixy::detail::bits_layout::TestU8;
using B = Bits<F>;

inline constexpr B b_empty{};
static_assert(b_empty.raw() == 0);
static_assert(b_empty.none());
static_assert(!b_empty.any());
static_assert(b_empty.popcount() == 0);

inline constexpr B b_single{F::A};
static_assert(b_single.raw() == 0x01);
static_assert(b_single.test(F::A));
static_assert(!b_single.test(F::B));
static_assert(b_single.popcount() == 1);

inline constexpr B b_multi{F::A, F::C, F::D};
static_assert(b_multi.raw() == (0x01 | 0x04 | 0x08));
static_assert(b_multi.test(F::A));
static_assert(!b_multi.test(F::B));
static_assert(b_multi.test(F::C));
static_assert(b_multi.test(F::D));
static_assert(b_multi.popcount() == 3);

inline constexpr B b_raw = B::from_raw(0x05);
static_assert(b_raw.raw() == 0x05);
static_assert(b_raw.test(F::A));
static_assert(b_raw.test(F::C));

[[nodiscard]] consteval bool mutate_works() noexcept {
    B b{};
    b.set(F::A);
    if (!b.test(F::A)) return false;
    b.set(F::B);
    if (b.popcount() != 2) return false;
    b.unset(F::A);
    if (b.test(F::A)) return false;
    if (!b.test(F::B)) return false;
    b.toggle(F::C);
    if (!b.test(F::C)) return false;
    b.toggle(F::C);
    if (b.test(F::C)) return false;
    b.clear();
    if (!b.none()) return false;
    return true;
}
static_assert(mutate_works());

[[nodiscard]] consteval bool equality_works() noexcept {
    B x{F::A, F::B};
    B y{F::B, F::A};
    B z{F::A, F::C};
    return (x == y) && !(x == z);
}
static_assert(equality_works());

[[nodiscard]] consteval bool bitwise_works() noexcept {
    B a{F::A, F::B};
    B b{F::B, F::C};
    B u = a | b;
    if (u.popcount() != 3) return false;
    B i = a & b;
    if (i.raw() != 0x02) return false;
    B x = a ^ b;
    if (x.raw() != (0x01 | 0x04)) return false;
    return true;
}
static_assert(bitwise_works());

[[nodiscard]] consteval bool complement_works() noexcept {
    B a{F::A};
    B na = ~a;
    return na.raw() == static_cast<std::uint8_t>(~static_cast<std::uint8_t>(0x01));
}
static_assert(complement_works());

[[nodiscard]] consteval bool compound_works() noexcept {
    B b{};
    b |= F::A;
    b |= F::C;
    if (b.popcount() != 2) return false;
    b &= B{F::A};
    if (b.test(F::C)) return false;
    if (!b.test(F::A)) return false;
    b ^= B{F::A};
    if (b.test(F::A)) return false;
    return true;
}
static_assert(compound_works());

[[nodiscard]] consteval bool enum_or_bits() noexcept {
    B b{F::A};
    B r1 = F::B | b;
    B r2 = b | F::B;
    return r1 == r2 && r1.popcount() == 2;
}
static_assert(enum_or_bits());

template <class B1, class B2>
concept can_or = requires(B1 a, B2 b) {
    { a | b };
};

using OtherB = Bits<::fixy::detail::bits_layout::TestU16>;
static_assert(!can_or<B, OtherB>, "Bits<E1> | Bits<E2> is a compile error.  Two enum-typed bit fields are "
                                  "different instantiations and the hidden friend operators only see "
                                  "same-instantiation pairs.  Without that rejection two unrelated flag "
                                  "enums mix silently at the call site.");

template <class B1, class B2>
concept can_eq = requires(B1 a, B2 b) {
    { a == b } -> std::convertible_to<bool>;
};
static_assert(!can_eq<B, OtherB>);

template <class W, class Lit>
concept can_construct_from = requires { W{std::declval<Lit>()}; };

static_assert(!can_construct_from<B, int>, "Bits<E>{42} does not compile.  The only public constructor takes an "
                                           "initializer_list<E>, and from_raw is the only raw escape.  Without "
                                           "that rejection raw integers leak into the typed surface.");

static_assert(!can_construct_from<B, std::uint8_t>,
              "Bits<E>{static_cast<uint8_t>(0x05)} does not compile.  Not even the "
              "exact underlying type enters through brace initialization.  Only the "
              "from_raw factory admits an underlying-type value.");

static_assert(!can_construct_from<B, ::fixy::detail::bits_layout::TestU16>,
              "Bits<TestU8>{TestU16::X} does not compile.  The initializer_list "
              "element type is fixed to the wrapper's own enum_type.");

[[nodiscard]] consteval bool raw_escape_works() noexcept {
    B b{F::B, F::C};
    return b.raw() == (0x02 | 0x04);
}
static_assert(raw_escape_works());

static_assert(std::is_trivially_copyable_v<B>);

static_assert(B::wrapper_kind() == "structural::Bits");

}  // namespace detail::bits_self_test

}  // namespace fixy
