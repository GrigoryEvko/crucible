#pragma once

// A flag word whose bits are named by one scoped enum.  Two Bits over
// different enums are different types and do not compose, which is what
// stops a flag from one enum being written into another's word.
//
// The wrapper does not know which flags exclude each other.  A pair of
// mutually exclusive flags can still be set together, and enforcing
// that would need a per-enum statement of which sets are exclusive.

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

}  // namespace fixy
