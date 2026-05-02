#pragma once

// ── crucible::safety::Saturated<T> ──────────────────────────────────
//
// Value-with-clamp-flag newtype for saturating arithmetic.  Wraps a
// T value paired with a boolean indicating whether the producing
// operation clamped to T's [min, max] range.  Carries the
// "did-this-saturate?" signal that bare `std::add_sat` / `mul_sat` /
// `crucible::sat::*` discard.
//
// Design intent: saturating arithmetic is the right answer for size
// computations, counter accumulators, and bounded budgets — but the
// caller often needs to know if saturation HAPPENED so it can:
//   - Surface a diagnostic ("budget exceeded; clamped to max")
//   - Take a divergent code path ("if storage overflowed, refuse")
//   - Update a statistics counter ("how often did we saturate?")
//
// Bare `add_sat(a, b)` → T loses that signal.  Saturated<T> preserves
// it as one bit at zero alignment cost (NSDMI-default-init false).
//
// ── Production call sites (per WRAP-* tasks) ────────────────────────
//
//   #999  WRAP-Saturate-1: Checked.h saturating_{add,sub,mul} return
//   #1018 WRAP-StorageNbytes-1: compute_storage_nbytes return
//   #1039 WRAP-TensorMeta-6: storage_nbytes field type
//
// ── Public API ──────────────────────────────────────────────────────
//
//   Construction:
//     Saturated()                          — NSDMI: value = T{}, clamped = false
//     Saturated(T v)                       — implicit-from-value (clamped = false)
//     Saturated(T v, bool was_clamped)     — explicit two-arg form
//
//   Accessors:
//     value()                              — T& / T const& access
//     was_clamped()                        — bool accessor
//     explicit operator T()                — escape (drops the flag)
//
//   Static factories (replace bare add_sat / sub_sat / mul_sat):
//     add_sat_checked(a, b)                — Saturated<T> result
//     sub_sat_checked(a, b)
//     mul_sat_checked(a, b)
//
//   Equality:
//     operator==                           — element-wise (value + clamped)
//
// ── Eight-axiom audit ───────────────────────────────────────────────
//
//   InitSafe — NSDMI: value = T{} (zero), clamped = false.  No
//              uninitialized read possible.
//   TypeSafe — distinct from bare T; explicit T conversion required to
//              escape (operator T is `explicit`, never implicit).
//   NullSafe — no pointers internally.
//   MemSafe  — no heap, no allocation.  Trivially_copyable when T is.
//   BorrowSafe — value type; per-instance, no aliasing surface.
//   ThreadSafe — value type; per-thread copies cheap.
//   LeakSafe — no resource ownership.
//   DetSafe  — pure structural; saturating arithmetic is deterministic
//              (clamp boundary is type-defined, not implementation-
//              defined).
//
// ── Runtime cost ────────────────────────────────────────────────────
//
//   sizeof(Saturated<T>) == sizeof(T) + 1 (clamped flag) + alignment
//   padding.  For T = uint64_t (8B): 16 bytes (8 + 1 + 7 padding).
//   For T = uint32_t (4B): 8 bytes (4 + 1 + 3 padding).  For T = char
//   (1B): 2 bytes (1 + 1, no padding).
//
//   The flag costs one byte; alignment-padding cost depends on T.
//   For pure value-extraction hot paths, callers `.value()` or
//   `static_cast<T>(saturated)` and the optimizer drops the flag.
//
//   Trivially_copyable when T is trivially_copyable; standard_layout
//   always.  memcpy-safe for serialization.
//
// ── Why structural (not Graded) ─────────────────────────────────────
//
// "Did this operation saturate?" is a structural marker on a value, not
// a graded modal property of the value itself.  No useful lattice
// applies — `clamped OR clamped` is just `clamped`, no algebraic
// structure beyond boolean OR.  Joins ConstantTime, Pinned, Machine,
// Bits, Borrowed/BorrowedRef, FixedArray as a deliberately-not-graded
// structural wrapper per CLAUDE.md §XVI.
//
// ── References ──────────────────────────────────────────────────────
//
//   CLAUDE.md §II        — 8 axioms
//   CLAUDE.md §XVI       — safety wrapper catalog (structural family)
//   CLAUDE.md §XVIII HS14 — neg-compile fixture requirement (≥2)
//   safety/Checked.h     — bare saturating_add/sub/mul primitives

#include <crucible/Platform.h>

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── Saturated<T> ──────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

template <typename T>
    requires std::is_arithmetic_v<T>
class [[nodiscard]] Saturated {
public:
    using value_type = T;

    static constexpr std::string_view wrapper_kind() noexcept {
        return "structural::Saturated";
    }

private:
    T    value_   = T{};      // ── NSDMI: zero-init (InitSafe) ──
    bool clamped_ = false;    // ── NSDMI: never-clamped default ──

public:
    // ── Construction ────────────────────────────────────────────────

    constexpr Saturated() noexcept = default;

    // Implicit from-value — represents an operation that did NOT
    // clamp (the common case for fresh values).  Implicit because
    // the caller using `Saturated<T> s = some_value;` clearly
    // means "no clamping observed".
    constexpr Saturated(T v) noexcept : value_{v}, clamped_{false} {}

    // Explicit two-arg form — used by saturating-arithmetic factories
    // below.  Two-arg construction must be explicit because the
    // semantics ("THIS clamping observation IS load-bearing") cannot
    // be inferred from the call site.
    constexpr explicit Saturated(T v, bool was_clamped) noexcept
        : value_{v}, clamped_{was_clamped} {}

    // Defaulted copy/move/dtor.
    constexpr Saturated(Saturated const&)            = default;
    constexpr Saturated(Saturated&&)                 = default;
    constexpr Saturated& operator=(Saturated const&) = default;
    constexpr Saturated& operator=(Saturated&&)      = default;
    ~Saturated()                                     = default;

    // ── Accessors ───────────────────────────────────────────────────

    [[nodiscard]] constexpr T const&  value() const& noexcept { return value_; }
    [[nodiscard]] constexpr T&        value() &      noexcept { return value_; }
    [[nodiscard]] constexpr T         value() &&     noexcept { return value_; }

    [[nodiscard]] constexpr bool was_clamped() const noexcept { return clamped_; }

    // Explicit T conversion — drops the clamped flag.  Marked
    // `explicit` so callers cannot accidentally lose the saturation
    // signal via implicit conversion.
    [[nodiscard]] constexpr explicit operator T() const noexcept { return value_; }

    // ── Equality (element-wise: value + clamped) ───────────────────
    //
    // Two Saturated<T> are equal iff BOTH the value AND the clamped
    // flag match.  Two distinct operations producing the same value
    // — one via clean arithmetic, one via clamping — are NOT equal,
    // because the clamping observation is semantically load-bearing.
    [[nodiscard]] friend constexpr bool operator==(
        Saturated const& a, Saturated const& b) noexcept = default;
};

// ═════════════════════════════════════════════════════════════════════
// ── Saturating arithmetic factories ───────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Produce Saturated<T> from saturating arithmetic, preserving the
// clamping observation.  Distinct from <safety/Checked.h>'s
// saturating_{add,sub,mul} which return bare T (clamping signal lost).
//
// Use these at the production sites listed in the docblock above
// (Saturate.h returns, compute_storage_nbytes, TensorMeta storage).
//
// All three are constexpr (compile-time-evaluable when args are
// constant) and use the GCC `__builtin_*_overflow` intrinsics for
// branchless overflow detection (one CPU flag check per operation).

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> add_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_add_overflow(a, b, &r)) [[unlikely]] {
        // Saturate to T's max or min depending on operand signs.
        if constexpr (std::is_signed_v<T>) {
            r = (b > T{0}) ? std::numeric_limits<T>::max()
                           : std::numeric_limits<T>::min();
        } else {
            // Unsigned overflow → always wraps past max → saturate to max.
            r = std::numeric_limits<T>::max();
        }
        return Saturated<T>{r, true};
    }
    return Saturated<T>{r, false};
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> sub_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_sub_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            r = (b > T{0}) ? std::numeric_limits<T>::min()
                           : std::numeric_limits<T>::max();
        } else {
            // Unsigned underflow → always wraps below 0 → saturate to 0.
            r = T{0};
        }
        return Saturated<T>{r, true};
    }
    return Saturated<T>{r, false};
}

template <std::integral T>
[[nodiscard]] constexpr Saturated<T> mul_sat_checked(T a, T b) noexcept {
    T r{};
    if (__builtin_mul_overflow(a, b, &r)) [[unlikely]] {
        if constexpr (std::is_signed_v<T>) {
            // Sign of saturation bound: positive if operands have same
            // sign (XOR-NOR-true), negative if opposite signs.
            bool same_sign = (a < T{0}) == (b < T{0});
            r = same_sign ? std::numeric_limits<T>::max()
                          : std::numeric_limits<T>::min();
        } else {
            r = std::numeric_limits<T>::max();
        }
        return Saturated<T>{r, true};
    }
    return Saturated<T>{r, false};
}

// ═════════════════════════════════════════════════════════════════════
// ── Layout invariants ─────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

// sizeof preserved at "T + 1 + alignment-padding".  Production-relevant
// instantiations:
static_assert(sizeof(Saturated<uint8_t>)  == 2);   // 1 + 1, no padding
static_assert(sizeof(Saturated<uint16_t>) == 4);   // 2 + 1 + 1 padding
static_assert(sizeof(Saturated<uint32_t>) == 8);   // 4 + 1 + 3 padding
static_assert(sizeof(Saturated<uint64_t>) == 16);  // 8 + 1 + 7 padding
static_assert(sizeof(Saturated<int8_t>)   == 2);
static_assert(sizeof(Saturated<int32_t>)  == 8);
static_assert(sizeof(Saturated<int64_t>)  == 16);

// alignof matches T (the larger member determines alignment).
static_assert(alignof(Saturated<uint64_t>) == alignof(uint64_t));
static_assert(alignof(Saturated<uint32_t>) == alignof(uint32_t));

// Trivially_copyable for arithmetic T (no user-defined ctor with
// non-trivial body; only NSDMI + defaulted operations).
static_assert(std::is_trivially_copyable_v<Saturated<uint64_t>>);
static_assert(std::is_trivially_copyable_v<Saturated<int64_t>>);
static_assert(std::is_trivially_destructible_v<Saturated<uint64_t>>);
static_assert(std::is_standard_layout_v<Saturated<uint64_t>>);

// Distinct type identity vs bare T (load-bearing — prevents accidental
// implicit drop of the clamped flag through an implicit conversion).
static_assert(!std::is_same_v<Saturated<uint64_t>, uint64_t>);
// Implicit conversion FROM T is allowed (constructor is implicit) for
// the common "fresh value, no clamping observed" case.  Implicit
// conversion TO T is NOT allowed (operator T is explicit).
static_assert( std::is_convertible_v<uint64_t, Saturated<uint64_t>>);
static_assert(!std::is_convertible_v<Saturated<uint64_t>, uint64_t>);

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::saturated_self_test {

using Sat64 = Saturated<uint64_t>;
using SatI32 = Saturated<int32_t>;

// Default ctor zero-initializes (NSDMI) — load-bearing.
[[nodiscard]] consteval bool default_zeroes() noexcept {
    Sat64 s{};
    return s.value() == 0 && !s.was_clamped();
}
static_assert(default_zeroes());

// Implicit-from-value ctor sets clamped = false.
[[nodiscard]] consteval bool implicit_from_value() noexcept {
    Sat64 s = uint64_t{42};
    return s.value() == 42 && !s.was_clamped();
}
static_assert(implicit_from_value());

// Explicit two-arg ctor preserves both fields.
[[nodiscard]] consteval bool explicit_two_arg() noexcept {
    Sat64 s{uint64_t{99}, true};
    return s.value() == 99 && s.was_clamped();
}
static_assert(explicit_two_arg());

// Explicit T conversion drops the flag but preserves value.
[[nodiscard]] consteval bool explicit_t_conversion() noexcept {
    Sat64 s{uint64_t{123}, true};
    auto v = static_cast<uint64_t>(s);
    return v == 123;
}
static_assert(explicit_t_conversion());

// Equality: same (value, clamped) → equal.  Different on either field
// → not equal.  This is load-bearing for the clamped semantics.
[[nodiscard]] consteval bool equality_pair_compare() noexcept {
    Sat64 a{uint64_t{5}, false};
    Sat64 b{uint64_t{5}, false};
    Sat64 c{uint64_t{5}, true};   // same value, different flag
    Sat64 d{uint64_t{6}, false};  // different value, same flag
    return (a == b) && !(a == c) && !(a == d);
}
static_assert(equality_pair_compare());

// add_sat_checked: no-overflow path returns clamped=false.
[[nodiscard]] consteval bool add_sat_no_overflow() noexcept {
    auto s = add_sat_checked<uint64_t>(100, 200);
    return s.value() == 300 && !s.was_clamped();
}
static_assert(add_sat_no_overflow());

// add_sat_checked: unsigned overflow saturates to max + clamped=true.
[[nodiscard]] consteval bool add_sat_unsigned_overflow() noexcept {
    auto s = add_sat_checked<uint8_t>(200, 100);  // 200+100=300 > 255
    return s.value() == std::numeric_limits<uint8_t>::max()
        && s.was_clamped();
}
static_assert(add_sat_unsigned_overflow());

// add_sat_checked: signed positive overflow saturates to max.
[[nodiscard]] consteval bool add_sat_signed_pos_overflow() noexcept {
    auto s = add_sat_checked<int8_t>(int8_t{100}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::max()
        && s.was_clamped();
}
static_assert(add_sat_signed_pos_overflow());

// add_sat_checked: signed negative overflow saturates to min.
[[nodiscard]] consteval bool add_sat_signed_neg_overflow() noexcept {
    auto s = add_sat_checked<int8_t>(int8_t{-100}, int8_t{-50});
    return s.value() == std::numeric_limits<int8_t>::min()
        && s.was_clamped();
}
static_assert(add_sat_signed_neg_overflow());

// sub_sat_checked: unsigned underflow saturates to 0.
[[nodiscard]] consteval bool sub_sat_unsigned_underflow() noexcept {
    auto s = sub_sat_checked<uint64_t>(5, 10);
    return s.value() == 0 && s.was_clamped();
}
static_assert(sub_sat_unsigned_underflow());

// sub_sat_checked: signed overflow (large positive - large negative).
[[nodiscard]] consteval bool sub_sat_signed_overflow() noexcept {
    auto s = sub_sat_checked<int8_t>(int8_t{100}, int8_t{-50});
    return s.value() == std::numeric_limits<int8_t>::max()
        && s.was_clamped();
}
static_assert(sub_sat_signed_overflow());

// mul_sat_checked: unsigned overflow saturates to max.
[[nodiscard]] consteval bool mul_sat_unsigned_overflow() noexcept {
    auto s = mul_sat_checked<uint8_t>(20, 20);  // 400 > 255
    return s.value() == std::numeric_limits<uint8_t>::max()
        && s.was_clamped();
}
static_assert(mul_sat_unsigned_overflow());

// mul_sat_checked: signed positive overflow saturates to max.
[[nodiscard]] consteval bool mul_sat_signed_pos_pos_overflow() noexcept {
    auto s = mul_sat_checked<int8_t>(int8_t{50}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::max()
        && s.was_clamped();
}
static_assert(mul_sat_signed_pos_pos_overflow());

// mul_sat_checked: signed negative-positive overflow saturates to min.
[[nodiscard]] consteval bool mul_sat_signed_neg_pos_overflow() noexcept {
    auto s = mul_sat_checked<int8_t>(int8_t{-50}, int8_t{50});
    return s.value() == std::numeric_limits<int8_t>::min()
        && s.was_clamped();
}
static_assert(mul_sat_signed_neg_pos_overflow());

// mul_sat_checked: no overflow returns Saturated{r, false}.
[[nodiscard]] consteval bool mul_sat_no_overflow() noexcept {
    auto s = mul_sat_checked<uint64_t>(7, 6);
    return s.value() == 42 && !s.was_clamped();
}
static_assert(mul_sat_no_overflow());

// Wrapper-kind diagnostic.
static_assert(Sat64::wrapper_kind() == "structural::Saturated");

// Saturated<float> instantiates (arithmetic concept admits float).
// Verify type-system properties only — the project bans FP `==`
// comparison via -Werror=float-equal, so we cannot test value
// equality at compile time without bit_cast gymnastics.  The fact
// that the concept admits float and the layout is well-defined
// is the load-bearing claim; bit-equality testing for FP would
// require __builtin_bit_cast<uint32_t>(f.value()) == 0x3FC00000u.
static_assert(std::is_arithmetic_v<float>);
static_assert(sizeof(Saturated<float>) == 8);  // 4 + 1 + 3 padding

// ── Runtime smoke test ──────────────────────────────────────────────

inline void runtime_smoke_test() {
    // Default ctor.
    Sat64 a{};
    if (a.value() != 0 || a.was_clamped()) std::abort();

    // Implicit from-value.
    Sat64 b = uint64_t{777};
    if (b.value() != 777 || b.was_clamped()) std::abort();

    // Explicit two-arg.
    Sat64 c{uint64_t{888}, true};
    if (c.value() != 888 || !c.was_clamped()) std::abort();

    // add_sat_checked: no-overflow.
    auto add_ok = add_sat_checked<uint64_t>(10, 20);
    if (add_ok.value() != 30 || add_ok.was_clamped()) std::abort();

    // add_sat_checked: overflow.
    auto add_clamped = add_sat_checked<uint8_t>(uint8_t{200}, uint8_t{100});
    if (add_clamped.value() != std::numeric_limits<uint8_t>::max()) std::abort();
    if (!add_clamped.was_clamped()) std::abort();

    // sub_sat_checked: underflow.
    auto sub_clamped = sub_sat_checked<uint64_t>(5, 10);
    if (sub_clamped.value() != 0 || !sub_clamped.was_clamped()) std::abort();

    // mul_sat_checked: overflow.
    auto mul_clamped = mul_sat_checked<uint8_t>(uint8_t{20}, uint8_t{20});
    if (mul_clamped.value() != std::numeric_limits<uint8_t>::max()) std::abort();
    if (!mul_clamped.was_clamped()) std::abort();

    // mul_sat_checked: no overflow.
    auto mul_ok = mul_sat_checked<uint64_t>(7, 6);
    if (mul_ok.value() != 42 || mul_ok.was_clamped()) std::abort();

    // Equality.
    Sat64 eq_a{uint64_t{5}, false};
    Sat64 eq_b{uint64_t{5}, false};
    Sat64 eq_c{uint64_t{5}, true};   // same value, different flag
    if (!(eq_a == eq_b)) std::abort();
    if (eq_a == eq_c) std::abort();   // flag differs → unequal

    // Explicit T conversion drops the flag.
    auto raw = static_cast<uint64_t>(eq_c);
    if (raw != 5) std::abort();

    // Trivially copyable verification at runtime.
    Sat64 src{uint64_t{0xDEADBEEFCAFEBABEull}, true};
    Sat64 dst;
    dst = src;
    if (dst.value() != src.value() || dst.was_clamped() != src.was_clamped()) {
        std::abort();
    }
}

}  // namespace detail::saturated_self_test

}  // namespace crucible::safety
