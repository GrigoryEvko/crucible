#pragma once

// ── crucible::effects::EffectMask + Row→EffectMask projection ───────
//
// Runtime projection from a type-level `effects::Row<Es...>` to a
// runtime-recordable `effects::EffectMask` value.  This is the load-
// bearing bridge between Crucible's THREE typed-set primitives:
//
//   safety::Bits<E>            runtime value-level set     bitwise (mask-enum)
//   effects::Row<Es...>        type-only set               consteval
//   safety::proto::PermSet<…>  type-only set of TYPE tags  consteval
//
// EffectMask is the dedicated runtime dual of `Row<Es...>`.  Used by
// Augur telemetry (FOUND-K01..K05), Cipher row-keying for federation,
// and any cross-process row announcement.
//
// ── Why a dedicated EffectMask, not safety::Bits<Effect> ────────────
//
// `Effect` is a POSITION-encoded enum (`Alloc = 0`, `IO = 1`, …
// `Test = 5`) per `effects/Capabilities.h`.  `safety::Bits<E>::set(e)`
// however treats `e` as a PRE-SHIFTED MASK and does `bits_ |= e`
// directly.  Composing them naively (`Bits<Effect>::set(Effect::Bg)`)
// would do `bits |= 3` (binary `0b11`), setting the wrong two bits
// (Alloc and IO instead of Bg).
//
// EffectMask is a small dedicated newtype that does the position→mask
// shift internally (`1u << position`).  It maintains the same mental
// model as Bits<E> but is purpose-built for position-encoded enums.
// Same eight-axiom audit, same trivially_copyable + standard-layout
// guarantees.
//
// ── Surface ─────────────────────────────────────────────────────────
//
//   EffectMask                     — runtime value-level set of Effect
//                                    atoms.  sizeof = sizeof(uint8_t).
//                                    Bit i = 1u << static_cast<u8>(e_i).
//
//   bits_for<Es...>()              — build EffectMask from a literal
//                                    pack of Effect values.  constexpr.
//   bits_from_row_pack(Row<Es...>) — internal pack-projector.
//   bits_from_row<R>()             — public surface; R must satisfy
//                                    IsEffectRow.
//   row_subsumes_bits<R>(sample)   — predicate: "does R cover every
//                                    bit set in sample?" (drift
//                                    detection: sample bit not in R
//                                    = drift).  constexpr.
//   bits_subsumes_row<R>(sample)   — inverse: "does sample cover every
//                                    bit declared in R?"  constexpr.
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — bits_ NSDMI = 0; default-constructed EffectMask is empty.
//   TypeSafe — Row<Es...> partial-spec rejects non-Row arguments at
//              the IsEffectRow concept gate.  EffectMask doesn't admit
//              raw integers in its construction surface (use raw()
//              for explicit escape, from_raw for explicit reconstruction).
//   NullSafe — no pointers.
//   MemSafe  — no allocation; trivially_copyable; standard_layout.
//   BorrowSafe — pure value; no aliasing surface.
//   ThreadSafe — pure value; per-thread copies are cheap.
//   LeakSafe — no resources.
//   DetSafe  — every operation is constexpr; output is bit-deterministic
//              across compilers (same Es... → same underlying integer).
//
// ── References ──────────────────────────────────────────────────────
//
//   effects/EffectRow.h       — Row<Es...> source
//   effects/Capabilities.h    — Effect enum (position-encoded)
//   safety/Bits.h             — sister wrapper (mask-encoded enums)
//   misc/28_04_2026_effects.md §1.2 superpower 5 (Augur drift)

#include <crucible/Platform.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>     // provides existing IsEffectRow concept

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

namespace crucible::effects {

// IsEffectRow concept lives in ExecCtx.h (effects::IsEffectRow,
// effects::is_effect_row_v).  We reuse it here rather than defining
// a duplicate.  Note: the existing concept does NOT cvref-strip, so
// callers must pass an unqualified Row<Es...> type to the projection
// functions below.

// ═════════════════════════════════════════════════════════════════════
// ── EffectMask — runtime value-level set of Effect atoms ──────────
// ═════════════════════════════════════════════════════════════════════

class [[nodiscard]] EffectMask {
public:
    using underlying_type = std::uint8_t;

private:
    underlying_type bits_{0};

    struct from_raw_tag_t {};
    constexpr EffectMask(from_raw_tag_t, underlying_type b) noexcept : bits_{b} {}

    [[nodiscard]] static constexpr underlying_type bit_for(Effect e) noexcept {
        return static_cast<underlying_type>(
            underlying_type{1} << static_cast<underlying_type>(e));
    }

public:
    constexpr EffectMask() noexcept = default;

    constexpr EffectMask(EffectMask const&)            = default;
    constexpr EffectMask(EffectMask&&)                 = default;
    constexpr EffectMask& operator=(EffectMask const&) = default;
    constexpr EffectMask& operator=(EffectMask&&)      = default;
    ~EffectMask()                                      = default;

    // Explicit raw escape — for deserialization paths or interop.
    [[nodiscard]] static constexpr EffectMask from_raw(underlying_type b) noexcept {
        return EffectMask{from_raw_tag_t{}, b};
    }

    constexpr void set(Effect e) noexcept {
        bits_ = static_cast<underlying_type>(bits_ | bit_for(e));
    }
    constexpr void unset(Effect e) noexcept {
        bits_ = static_cast<underlying_type>(
            bits_ & static_cast<underlying_type>(~bit_for(e)));
    }
    constexpr void clear() noexcept { bits_ = 0; }

    [[nodiscard]] constexpr bool test(Effect e) const noexcept {
        return (bits_ & bit_for(e)) != underlying_type{0};
    }
    [[nodiscard]] constexpr bool none() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr bool any()  const noexcept { return bits_ != 0; }
    [[nodiscard]] constexpr int  popcount() const noexcept {
        return std::popcount(bits_);
    }
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

// ═════════════════════════════════════════════════════════════════════
// ── bits_for<Es...>() — direct from a pack of Effect values ───────
// ═════════════════════════════════════════════════════════════════════

template <Effect... Es>
[[nodiscard]] constexpr EffectMask bits_for() noexcept {
    EffectMask result{};
    (result.set(Es), ...);
    return result;
}

// ═════════════════════════════════════════════════════════════════════
// ── bits_from_row_pack(Row<Es...>) — internal pack projector ──────
// ═════════════════════════════════════════════════════════════════════

template <Effect... Es>
[[nodiscard]] constexpr EffectMask
bits_from_row_pack(Row<Es...>) noexcept
{
    return bits_for<Es...>();
}

// ═════════════════════════════════════════════════════════════════════
// ── bits_from_row<R>() — public type-driven projection ────────────
// ═════════════════════════════════════════════════════════════════════

template <IsEffectRow R>
[[nodiscard]] constexpr EffectMask bits_from_row() noexcept {
    return bits_from_row_pack(R{});
}

// ═════════════════════════════════════════════════════════════════════
// ── Row × EffectMask subsumption predicates ──────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// row_subsumes_bits<R>(sample):
//   "does R cover every bit set in sample?"  i.e., is sample ⊆ row?
//   Augur drift detection — sample contains an effect not declared
//   in row R = drift = false.
//
// bits_subsumes_row<R>(sample):
//   inverse — "does sample cover every bit declared in R?"  i.e.,
//   is row ⊆ sample?

template <IsEffectRow R>
[[nodiscard]] constexpr bool
row_subsumes_bits(EffectMask sample) noexcept
{
    auto row_bits = bits_from_row<R>();
    auto outside  = sample & ~row_bits;
    return outside.none();
}

template <IsEffectRow R>
[[nodiscard]] constexpr bool
bits_subsumes_row(EffectMask sample) noexcept
{
    auto row_bits = bits_from_row<R>();
    auto missing  = row_bits & ~sample;
    return missing.none();
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::effect_row_projection_self_test {

// IsEffectRow detection (concept reused from ExecCtx.h — does NOT
// cvref-strip; callers pass unqualified Row types).
static_assert( IsEffectRow<Row<>>);
static_assert( IsEffectRow<Row<Effect::Bg>>);
static_assert( IsEffectRow<Row<Effect::Bg, Effect::Alloc>>);
static_assert(!IsEffectRow<int>);
static_assert(!IsEffectRow<Effect>);
static_assert(!IsEffectRow<EffectMask>);

// bits_for<Es...> position-shifted bit math.
static_assert(bits_for<>().none());
static_assert(bits_for<>().popcount() == 0);

static_assert(bits_for<Effect::Alloc>().raw() == (1u << 0));   // Alloc = position 0
static_assert(bits_for<Effect::IO>().raw()    == (1u << 1));
static_assert(bits_for<Effect::Bg>().raw()    == (1u << 3));   // Bg = position 3
static_assert(bits_for<Effect::Test>().raw()  == (1u << 5));

static_assert(bits_for<Effect::Bg>().test(Effect::Bg));
static_assert(!bits_for<Effect::Bg>().test(Effect::Alloc));
static_assert(bits_for<Effect::Bg>().popcount() == 1);

static_assert(bits_for<Effect::Bg, Effect::Alloc>().test(Effect::Bg));
static_assert(bits_for<Effect::Bg, Effect::Alloc>().test(Effect::Alloc));
static_assert(bits_for<Effect::Bg, Effect::Alloc>().popcount() == 2);
static_assert(bits_for<Effect::Bg, Effect::Alloc>().raw() == ((1u << 3) | (1u << 0)));

// bits_from_row<R> projection.
static_assert(bits_from_row<Row<>>().none());
static_assert(bits_from_row<Row<Effect::IO>>().test(Effect::IO));
static_assert(bits_from_row<Row<Effect::IO>>().popcount() == 1);

// Order-insensitive: bitwise OR is commutative.
static_assert(bits_from_row<Row<Effect::Bg, Effect::Alloc>>()
              == bits_from_row<Row<Effect::Alloc, Effect::Bg>>(),
    "bits_from_row MUST be order-insensitive — bitwise OR is "
    "commutative.  If this fires, federation peers projecting "
    "different Row orderings would compute different cache keys.");

// from_raw round-trip.
static_assert(EffectMask::from_raw(0x0B).raw() == 0x0B);

// row_subsumes_bits — drift-detection semantics.
[[nodiscard]] consteval bool drift_detection() noexcept {
    using R_bg_only = Row<Effect::Bg>;
    auto sample_bg_alloc = bits_for<Effect::Bg, Effect::Alloc>();
    if (row_subsumes_bits<R_bg_only>(sample_bg_alloc)) return false;   // drift
    auto sample_bg_only = bits_for<Effect::Bg>();
    if (!row_subsumes_bits<R_bg_only>(sample_bg_only)) return false;   // covered
    if (!row_subsumes_bits<R_bg_only>(EffectMask{})) return false;     // empty trivially
    return true;
}
static_assert(drift_detection());

// bits_subsumes_row — inverse subsumption.
[[nodiscard]] consteval bool inverse_subsumption() noexcept {
    using R_bg_alloc = Row<Effect::Bg, Effect::Alloc>;
    auto sample_full = bits_for<Effect::Bg, Effect::Alloc, Effect::IO>();
    if (!bits_subsumes_row<R_bg_alloc>(sample_full)) return false;    // covers
    auto sample_partial = bits_for<Effect::Bg>();
    if (bits_subsumes_row<R_bg_alloc>(sample_partial)) return false;  // missing Alloc
    return true;
}
static_assert(inverse_subsumption());

// Empty Row trivially subsumed by any sample.
static_assert(row_subsumes_bits<Row<>>(EffectMask{}));
// Empty row CANNOT subsume a non-empty sample (load-bearing semantics).
static_assert(!row_subsumes_bits<Row<>>(bits_for<Effect::Bg>()),
    "An empty row R = Row<> has no atoms, so it does NOT cover a "
    "sample containing Effect::Bg — drift expected.  If this fires, "
    "row_subsumes_bits has the subsumption direction inverted.");

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    auto a = bits_for<Effect::Bg>();
    auto b = bits_from_row<Row<Effect::Bg>>();
    if (a != b) std::abort();

    auto multi = bits_from_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
    if (multi.popcount() != 3) std::abort();
    if (!multi.test(Effect::Bg))    std::abort();
    if (!multi.test(Effect::Alloc)) std::abort();
    if (!multi.test(Effect::IO))    std::abort();

    using R_bg = Row<Effect::Bg>;
    auto sample_bg_io = bits_for<Effect::Bg, Effect::IO>();
    if (row_subsumes_bits<R_bg>(sample_bg_io)) std::abort();   // drift expected

    auto sample_bg = bits_for<Effect::Bg>();
    if (!row_subsumes_bits<R_bg>(sample_bg)) std::abort();   // covered

    using R_bg_alloc = Row<Effect::Bg, Effect::Alloc>;
    if (bits_subsumes_row<R_bg_alloc>(sample_bg)) std::abort();   // missing Alloc
    auto sample_full = bits_for<Effect::Bg, Effect::Alloc>();
    if (!bits_subsumes_row<R_bg_alloc>(sample_full)) std::abort();

    // Round-trip via from_raw (deserialization path).
    auto serialized = sample_full.raw();
    auto round = EffectMask::from_raw(serialized);
    if (round != sample_full) std::abort();
}

}  // namespace detail::effect_row_projection_self_test

}  // namespace crucible::effects
