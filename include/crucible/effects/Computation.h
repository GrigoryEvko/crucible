#pragma once

// ── crucible::effects::Computation<Row, T> ──────────────────────────
//
// The carrier type for the Met(X) effect-row algebra per Tang-Lindley
// POPL 2026 (arXiv:2507.10301) and 25_04_2026.md §3.2.  A
// `Computation<R, T>` is a value of type T whose evaluation may use
// the effects in row R.  Foreground hot-path code holds
// `Computation<EmptyRow, T>` and cannot perform any effectful action;
// the background thread holds `Computation<Row<Effect::Bg>, T>` and
// transitively gains every Bg-implied effect (Alloc, IO, Block).
//
//   Axiom coverage: TypeSafe — capability propagation is one
//                   `Subrow` requires-clause per call site; the type
//                   system enforces propagation, not discipline.
//                   DetSafe — every row operation is consteval.
//   Runtime cost:   zero.  T is the only stored field; the row is
//                   purely a type-level tag.  EBO via
//                   `[[no_unique_address]]` collapses Computation to
//                   sizeof(T) when the parent uses
//                   `[[no_unique_address]]` on its Computation member.
//
// STATUS: stub.  Class type is COMPLETE so that the METX-4 compat
// shim and any METX-5-touched call sites compile NOW.  Operation
// bodies land in METX-1 (#473); calling any of the deleted methods
// fails with the implementation-task pointer.
//
// Operations to land per METX-1 (#473):
//   mk(T)              — lift a pure T into Computation<EmptyRow, T>
//   extract()          — collapse Computation<EmptyRow, T> back to T
//   lift<Cap>(T)       — construct at row {Cap}
//   weaken<R₂>()       — widen row when Subrow<R, R₂>
//   then(k)            — bind: T -> Computation<R', U> ⊕ R into R'
//   map(f)             — fmap: T -> U preserving row
//
// See Capabilities.h (Effect atoms), EffectRow.h (Row + Subrow algebra),
// compat/Fx.h (backward-compat fx::* aliases).

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <string_view>
#include <type_traits>

namespace crucible::effects {

// ── Computation<R, T> ───────────────────────────────────────────────
template <typename R, typename T>
class [[nodiscard]] Computation {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using row_type   = R;
    using value_type = T;

    static constexpr std::size_t row_size = row_size_v<R>;

    // ── Layout (NSDMI per InitSafe) ─────────────────────────────────
    [[no_unique_address]] T inner_{};

    // ── Diagnostic ──────────────────────────────────────────────────
    //
    // Static rather than per-instance; the row is type-level only.
    [[nodiscard]] static consteval std::size_t effect_count_in_row() noexcept {
        return row_size_v<R>;
    }

    // ── Object semantics (defaulted; see Graded.h for the rationale
    //     on NOT specifying explicit noexcept on `= default`) ─────────
    constexpr Computation()                              = default;
    constexpr Computation(const Computation&)            = default;
    constexpr Computation(Computation&&)                 = default;
    constexpr Computation& operator=(const Computation&) = default;
    constexpr Computation& operator=(Computation&&)      = default;
    ~Computation()                                       = default;

    // ── Public operations (declared; bodies in METX-1 #473) ─────────
    //
    // Same `= delete("reason")` discipline as Graded.h: the API
    // surface is reachable for type-system queries and alias
    // declarations; calls fail at use site with the implementation-
    // task pointer in the diagnostic.

    [[nodiscard]] static constexpr Computation mk(T x) noexcept
        requires (row_size_v<R> == 0)
        = delete("Computation::mk: implementation deferred to METX-1 (#473)");

    [[nodiscard]] constexpr T extract() const noexcept
        requires (row_size_v<R> == 0)
        = delete("Computation::extract: implementation deferred to METX-1 (#473)");

    template <Effect Cap>
    [[nodiscard]] static constexpr auto lift(T x) noexcept
        -> Computation<Row<Cap>, T>
        = delete("Computation::lift: implementation deferred to METX-1 (#473)");

    template <typename R2>
        requires Subrow<R, R2>
    [[nodiscard]] constexpr Computation<R2, T> weaken() const noexcept
        = delete("Computation::weaken: implementation deferred to METX-1 (#473)");

    // `then` and `map` deferred entirely — their signatures depend on
    // row_union_t which is itself deferred to METX-2 (#474).  Adding
    // them now would risk binding their signatures to the wrong shape.
};

// ── Layout invariant macro (used by METX-4 compat aliases) ─────────
#define CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationAlias, T_)         \
    static_assert(sizeof(ComputationAlias<T_>) == sizeof(T_),               \
                  "Computation alias " #ComputationAlias " over " #T_       \
                  " violates the zero-overhead contract; review "           \
                  "[[no_unique_address]] usage")

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::computation_self_test {

struct EmptyValue {};
struct OneByteValue { char c{0}; };
struct EightByteValue { unsigned long long v{0}; };

using C_empty       = Computation<Row<>, EmptyValue>;
using C_one_byte    = Computation<Row<>, OneByteValue>;
using C_eight_byte  = Computation<Row<Effect::Bg>, EightByteValue>;

// Type aliases reachable.
static_assert(std::is_same_v<C_empty::row_type,   Row<>>);
static_assert(std::is_same_v<C_empty::value_type, EmptyValue>);

// Default-constructible.
static_assert(std::is_default_constructible_v<C_empty>);
static_assert(std::is_default_constructible_v<C_one_byte>);
static_assert(std::is_default_constructible_v<C_eight_byte>);

// Layout — the row is type-level only, no runtime cost.
static_assert(sizeof(C_empty)      == 1);
static_assert(sizeof(C_one_byte)   == sizeof(OneByteValue));
static_assert(sizeof(C_eight_byte) == sizeof(EightByteValue));

// Effect counts on row reachable.
static_assert(C_empty::effect_count_in_row()      == 0);
static_assert(C_one_byte::effect_count_in_row()   == 0);
static_assert(C_eight_byte::effect_count_in_row() == 1);

// Layout invariant macro fires correctly.
template <typename T>
using ComputationOverEmptyRow = Computation<Row<>, T>;
CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationOverEmptyRow, OneByteValue);
CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT(ComputationOverEmptyRow, EightByteValue);

}  // namespace detail::computation_self_test

}  // namespace crucible::effects
