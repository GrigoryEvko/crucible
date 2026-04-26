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
// STATUS: METX-1 (#473) bodies SHIPPED for mk / extract / lift / weaken.
//   `then` / `map` remain deferred — their signatures depend on
//   `row_union_t` (now live, METX-2 #474), so they can land in a
//   follow-up commit once Computation has at least one production
//   caller.  Shipping `then`/`map` without a caller risks binding the
//   row-arithmetic policy (left-bias vs. right-bias on duplicate
//   atoms) to the wrong shape; better to wait for a concrete demand.
//
// Operations now live:
//   mk(T)              — lift a pure T into Computation<EmptyRow, T>
//   extract()          — collapse Computation<EmptyRow, T> back to T
//                         (lvalue → const T&; rvalue → T)
//   lift<Cap>(T)       — construct at row {Cap}
//   weaken<R₂>()       — widen row when Subrow<R, R₂>
//                         (lvalue → copy; rvalue → move)
//
// Operations still deferred:
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

    // ── Value-taking constructor ────────────────────────────────────
    //
    // `explicit` so an implicit conversion T → Computation<R, T> can
    // never happen — every lift is visible at the source.  Required
    // by mk / lift / weaken which all funnel construction through it.
    explicit constexpr Computation(T x)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : inner_{std::move(x)} {}

    // ── Public operations (METX-1 #473 bodies) ──────────────────────
    //
    // `mk` — pure-value lift into the empty row.  Concrete row R must
    // BE the empty row (substitution principle is the caller's job at
    // alias-declaration time; here we only admit the no-effect case).
    [[nodiscard]] static constexpr Computation mk(T x)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        requires (row_size_v<R> == 0)
    {
        return Computation{std::move(x)};
    }

    // `extract` — unwrap a pure value out of an empty-row Computation.
    // Two overloads: lvalue returns const-ref (no copy on inspection),
    // rvalue returns by value (move out of inner_).
    [[nodiscard]] constexpr const T& extract() const & noexcept
        requires (row_size_v<R> == 0)
    {
        return inner_;
    }

    [[nodiscard]] constexpr T extract() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
        requires (row_size_v<R> == 0)
    {
        return std::move(inner_);
    }

    // `lift<Cap>` — construct a Computation at row {Cap} from a raw T.
    // Static (the current row R is irrelevant to the result type).
    // Explicit `IsEffect<Cap>` requires-clause rejects template typos
    // at substitution time, not at use site.
    template <Effect Cap>
        requires IsEffect<Cap>
    [[nodiscard]] static constexpr auto lift(T x)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        -> Computation<Row<Cap>, T>
    {
        return Computation<Row<Cap>, T>{std::move(x)};
    }

    // `weaken<R2>` — widen the row when Subrow<R, R2>.  This IS the
    // substitution principle: a function declared at row R may be
    // satisfied by a Computation declared at any narrower row.  Two
    // overloads to avoid an unnecessary copy on rvalue uses.
    template <typename R2>
        requires Subrow<R, R2>
    [[nodiscard]] constexpr Computation<R2, T> weaken() const &
        noexcept(std::is_nothrow_copy_constructible_v<T>)
    {
        return Computation<R2, T>{inner_};
    }

    template <typename R2>
        requires Subrow<R, R2>
    [[nodiscard]] constexpr Computation<R2, T> weaken() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Computation<R2, T>{std::move(inner_)};
    }

    // `then` / `map` deferred entirely — their signatures depend on
    // row_union_t (now live; see EffectRow.h §METX-2).  Bind/fmap
    // semantics for Met(X) are sensitive to whether the bind operator
    // is left-biased or right-biased over the row arithmetic; this is
    // a policy choice that should follow a concrete production caller,
    // not anticipate one.  See METX backlog for the follow-up.
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

// ── Operation coverage (METX-1 #473 bodies) ─────────────────────────
//
// `mk` → `extract` round-trip for the empty row.
static_assert([] consteval {
    auto pure = Computation<Row<>, int>::mk(42);
    return pure.extract() == 42;
}(),
"mk/extract round-trip on Computation<EmptyRow, int> failed.");

// `lift<Cap>` from a pure value into a single-effect row.
static_assert([] consteval {
    auto bg = Computation<Row<>, int>::lift<Effect::Bg>(7);
    using BgComp = decltype(bg);
    return std::is_same_v<BgComp, Computation<Row<Effect::Bg>, int>>;
}(),
"lift<Bg> did not produce Computation<Row<Bg>, T>.");

// `weaken` widens by Subrow.  Empty → {Bg} → {Bg, Alloc, IO} chain.
static_assert([] consteval {
    auto bg     = Computation<Row<>, int>::lift<Effect::Bg>(13);
    auto wider  = bg.template weaken<Row<Effect::Bg, Effect::Alloc>>();
    auto widest = wider.template weaken<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
    return widest.inner_ == 13;
}(),
"weaken-chain through nested Subrow did not preserve the inner value.");

// `weaken` rvalue overload moves instead of copies (correctness check
// via consteval round-trip; the move-vs-copy choice is observable in
// nothrow inference, exercised by the compile-time noexcept assertion).
static_assert(
    noexcept(std::declval<Computation<Row<>, int>>().template weaken<Row<Effect::Bg>>()),
    "Computation<R, int>::weaken() && must be noexcept for trivially-"
    "move-constructible payloads.");

// extract() rvalue overload moves; correctness checked by replicating
// the value with a move-only-equivalent payload (int suffices for
// noexcept inference).
static_assert(
    noexcept(std::declval<Computation<Row<>, int>>().extract()),
    "Computation<EmptyRow, int>::extract() && must be noexcept for trivially-"
    "move-constructible payloads.");

}  // namespace detail::computation_self_test

// ── Runtime smoke test ──────────────────────────────────────────────
//
// Per project discipline: every algebra/* and effects/* header MUST
// ship `inline void runtime_smoke_test()` exercising bodies with non-
// constant arguments (consteval-only coverage misses lvalue/rvalue
// overload selection and noexcept-propagation bugs that fire only at
// instantiation time on real values).  This is the load-bearing
// discipline established by feedback_algebra_runtime_smoke_test_
// discipline.md after the ALGEBRA-1..11 audit.
inline void runtime_smoke_test_computation() {
    // mk + extract through both lvalue and rvalue.
    auto pure_lvalue = Computation<Row<>, int>::mk(100);
    int  read_lvalue = pure_lvalue.extract();          // const T& overload
    int  read_rvalue = std::move(pure_lvalue).extract();  // T&& overload
    (void)read_lvalue;
    (void)read_rvalue;

    // lift into a single-effect row.
    auto bg_pure = Computation<Row<>, int>::lift<Effect::Bg>(200);
    static_assert(std::is_same_v<
        decltype(bg_pure),
        Computation<Row<Effect::Bg>, int>
    >);

    // weaken to a strictly-larger row through both lvalue and rvalue.
    auto bg_widened_lvalue =
        bg_pure.template weaken<Row<Effect::Bg, Effect::Alloc>>();
    auto bg_widened_rvalue =
        std::move(bg_pure).template weaken<Row<Effect::Bg, Effect::IO>>();
    (void)bg_widened_lvalue;
    (void)bg_widened_rvalue;
}

}  // namespace crucible::effects
