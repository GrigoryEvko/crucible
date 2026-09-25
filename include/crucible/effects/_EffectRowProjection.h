#pragma once

// EffectMask is the runtime dual of the type-level `Row<Es...>`: a
// value-level set of Effect atoms that can be recorded, transmitted and
// compared against a declared row.
//
// The sibling bit-set wrapper in the safety tree is not reusable here.
// It takes each enumerator as an already-shifted mask and ORs it in
// directly, whereas Effect is position-encoded, so `set(Effect::Bg)`
// would set bits 0 and 1 rather than bit 3.  This newtype does the
// position-to-mask shift itself.

#include <crucible/Platform.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/safety/_Pre.h>

#include <bit>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

namespace crucible::effects {

// IsEffectRow does not strip cv-qualifiers or references, so callers
// pass an unqualified Row type to the projections below.

class [[nodiscard]] EffectMask {
public:
    using underlying_type = std::uint8_t;

private:
    underlying_type bits_{0};

    struct from_raw_tag_t {};
    constexpr EffectMask(from_raw_tag_t, underlying_type b) noexcept : bits_{b} {}

    [[nodiscard]] static constexpr underlying_type bit_for(Effect e) noexcept {
        return static_cast<underlying_type>(underlying_type{1} << static_cast<underlying_type>(e));
    }

public:
    constexpr EffectMask() noexcept = default;

    constexpr EffectMask(EffectMask const&) = default;
    constexpr EffectMask(EffectMask&&) = default;
    constexpr EffectMask& operator=(EffectMask const&) = default;
    constexpr EffectMask& operator=(EffectMask&&) = default;
    ~EffectMask() = default;

    // Effect occupies positions 0 through effect_count - 1, so
    // `(1 << effect_count) - 1` is the full valid-bit mask.  A bit above
    // that names no atom and is poison from a buggy or hostile peer.
    // Without the mask it would round-trip back out through `raw()`.
    //
    // The sanitizing AND must not be paired with a claim that it is a
    // no-op.  The project precondition macro lowers to
    // `[[assume(sanitized == b)]]` under NDEBUG, which lets the
    // optimizer substitute `b` for `sanitized`, leaving the AND dead
    // and deleting it.  Release builds would then pass the high bits
    // straight through.  A plain `assert` carries no such assumption,
    // so the AND survives every build mode, and the `if consteval`
    // branch keeps the compile-time rejection the macro would give.
    [[nodiscard]] static constexpr EffectMask from_raw(underlying_type b) noexcept {
        constexpr underlying_type valid_mask =
            static_cast<underlying_type>((underlying_type{1} << effect_count) - underlying_type{1});
        underlying_type const sanitized = static_cast<underlying_type>(b & valid_mask);
        // `__builtin_trap` is not a constant expression, so a
        // compile-time call with a poisoned input is a hard error.
        if consteval {
            if (sanitized != b) {
                __builtin_trap();
            }
        }
        assert(sanitized == b);
        return EffectMask{from_raw_tag_t{}, sanitized};
    }

    constexpr void set(Effect e) noexcept { bits_ = static_cast<underlying_type>(bits_ | bit_for(e)); }
    constexpr void unset(Effect e) noexcept {
        bits_ = static_cast<underlying_type>(bits_ & static_cast<underlying_type>(~bit_for(e)));
    }
    constexpr void clear() noexcept { bits_ = 0; }

    [[nodiscard]] constexpr bool test(Effect e) const noexcept { return (bits_ & bit_for(e)) != underlying_type{0}; }
    [[nodiscard]] constexpr bool none() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr bool any() const noexcept { return bits_ != 0; }
    [[nodiscard]] constexpr int popcount() const noexcept { return std::popcount(bits_); }
    [[nodiscard]] constexpr underlying_type raw() const noexcept { return bits_; }

    [[nodiscard]] friend constexpr bool operator==(EffectMask, EffectMask) noexcept = default;

    [[nodiscard]] friend constexpr EffectMask operator|(EffectMask a, EffectMask b) noexcept {
        return EffectMask{from_raw_tag_t{}, static_cast<underlying_type>(a.bits_ | b.bits_)};
    }
    [[nodiscard]] friend constexpr EffectMask operator&(EffectMask a, EffectMask b) noexcept {
        return EffectMask{from_raw_tag_t{}, static_cast<underlying_type>(a.bits_ & b.bits_)};
    }
    [[nodiscard]] friend constexpr EffectMask operator^(EffectMask a, EffectMask b) noexcept {
        return EffectMask{from_raw_tag_t{}, static_cast<underlying_type>(a.bits_ ^ b.bits_)};
    }
    [[nodiscard]] friend constexpr EffectMask operator~(EffectMask a) noexcept {
        return EffectMask{from_raw_tag_t{}, static_cast<underlying_type>(~a.bits_)};
    }
};

static_assert(sizeof(EffectMask) == sizeof(std::uint8_t));
static_assert(std::is_trivially_copyable_v<EffectMask>);
static_assert(std::is_standard_layout_v<EffectMask>);

template <Effect... Es>
[[nodiscard]] constexpr EffectMask bits_for() noexcept {
    EffectMask result{};
    (result.set(Es), ...);
    return result;
}

template <Effect... Es>
[[nodiscard]] constexpr EffectMask bits_from_row_pack(Row<Es...>) noexcept {
    return bits_for<Es...>();
}

template <IsEffectRow R>
[[nodiscard]] constexpr EffectMask bits_from_row() noexcept {
    return bits_from_row_pack(R{});
}

// A sample bit outside the declared row is effect drift.
template <IsEffectRow R>
[[nodiscard]] constexpr bool row_subsumes_bits(EffectMask sample) noexcept {
    auto row_bits = bits_from_row<R>();
    auto outside = sample & ~row_bits;
    return outside.none();
}

template <IsEffectRow R>
[[nodiscard]] constexpr bool bits_subsumes_row(EffectMask sample) noexcept {
    auto row_bits = bits_from_row<R>();
    auto missing = row_bits & ~sample;
    return missing.none();
}

namespace detail::effect_row_projection_self_test {

static_assert(IsEffectRow<Row<>>);
static_assert(IsEffectRow<Row<Effect::Bg>>);
static_assert(IsEffectRow<Row<Effect::Bg, Effect::Alloc>>);
static_assert(!IsEffectRow<int>);
static_assert(!IsEffectRow<Effect>);
static_assert(!IsEffectRow<EffectMask>);

static_assert(bits_for<>().none());
static_assert(bits_for<>().popcount() == 0);

static_assert(bits_for<Effect::Alloc>().raw() == (1u << 0));
static_assert(bits_for<Effect::IO>().raw() == (1u << 1));
static_assert(bits_for<Effect::Bg>().raw() == (1u << 3));
static_assert(bits_for<Effect::Test>().raw() == (1u << 5));

static_assert(bits_for<Effect::Bg>().test(Effect::Bg));
static_assert(!bits_for<Effect::Bg>().test(Effect::Alloc));
static_assert(bits_for<Effect::Bg>().popcount() == 1);

static_assert(bits_for<Effect::Bg, Effect::Alloc>().test(Effect::Bg));
static_assert(bits_for<Effect::Bg, Effect::Alloc>().test(Effect::Alloc));
static_assert(bits_for<Effect::Bg, Effect::Alloc>().popcount() == 2);
static_assert(bits_for<Effect::Bg, Effect::Alloc>().raw() == ((1u << 3) | (1u << 0)));

static_assert(bits_from_row<Row<>>().none());
static_assert(bits_from_row<Row<Effect::IO>>().test(Effect::IO));
static_assert(bits_from_row<Row<Effect::IO>>().popcount() == 1);

static_assert(bits_from_row<Row<Effect::Bg, Effect::Alloc>>() == bits_from_row<Row<Effect::Alloc, Effect::Bg>>(),
              "bits_from_row MUST be order-insensitive — bitwise OR is "
              "commutative.  If this fires, federation peers projecting "
              "different Row orderings would compute different cache keys.");

static_assert(EffectMask::from_raw(0x0B).raw() == 0x0B);

static_assert(EffectMask::from_raw(0x3F).raw() == 0x3F);
static_assert(EffectMask::from_raw(0x00).raw() == 0x00);
static_assert(EffectMask::from_raw(0x20).raw() == 0x20);
static_assert(EffectMask::from_raw(0x01).raw() == 0x01);

[[nodiscard]] consteval bool drift_detection() noexcept {
    using R_bg_only = Row<Effect::Bg>;
    auto sample_bg_alloc = bits_for<Effect::Bg, Effect::Alloc>();
    if (row_subsumes_bits<R_bg_only>(sample_bg_alloc)) return false;
    auto sample_bg_only = bits_for<Effect::Bg>();
    if (!row_subsumes_bits<R_bg_only>(sample_bg_only)) return false;
    if (!row_subsumes_bits<R_bg_only>(EffectMask{})) return false;
    return true;
}
static_assert(drift_detection());

[[nodiscard]] consteval bool inverse_subsumption() noexcept {
    using R_bg_alloc = Row<Effect::Bg, Effect::Alloc>;
    auto sample_full = bits_for<Effect::Bg, Effect::Alloc, Effect::IO>();
    if (!bits_subsumes_row<R_bg_alloc>(sample_full)) return false;
    auto sample_partial = bits_for<Effect::Bg>();
    if (bits_subsumes_row<R_bg_alloc>(sample_partial)) return false;
    return true;
}
static_assert(inverse_subsumption());

static_assert(row_subsumes_bits<Row<>>(EffectMask{}));
static_assert(!row_subsumes_bits<Row<>>(bits_for<Effect::Bg>()),
              "An empty row R = Row<> has no atoms, so it does NOT cover a "
              "sample containing Effect::Bg — drift expected.  If this fires, "
              "row_subsumes_bits has the subsumption direction inverted.");

inline void runtime_smoke_test() {
    auto a = bits_for<Effect::Bg>();
    auto b = bits_from_row<Row<Effect::Bg>>();
    if (a != b) std::abort();

    auto multi = bits_from_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
    if (multi.popcount() != 3) std::abort();
    if (!multi.test(Effect::Bg)) std::abort();
    if (!multi.test(Effect::Alloc)) std::abort();
    if (!multi.test(Effect::IO)) std::abort();

    using R_bg = Row<Effect::Bg>;
    auto sample_bg_io = bits_for<Effect::Bg, Effect::IO>();
    if (row_subsumes_bits<R_bg>(sample_bg_io)) std::abort();

    auto sample_bg = bits_for<Effect::Bg>();
    if (!row_subsumes_bits<R_bg>(sample_bg)) std::abort();

    using R_bg_alloc = Row<Effect::Bg, Effect::Alloc>;
    if (bits_subsumes_row<R_bg_alloc>(sample_bg)) std::abort();
    auto sample_full = bits_for<Effect::Bg, Effect::Alloc>();
    if (!bits_subsumes_row<R_bg_alloc>(sample_full)) std::abort();

    auto serialized = sample_full.raw();
    auto round = EffectMask::from_raw(serialized);
    if (round != sample_full) std::abort();
}

}  // namespace detail::effect_row_projection_self_test

}  // namespace crucible::effects
