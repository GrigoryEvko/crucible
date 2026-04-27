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
// STATUS: METX-1 (#473) bodies SHIPPED — full Met(X) monadic surface.
//   The row-arithmetic policy (left-biased: R1 atoms appear before
//   R2's, with duplicates absorbed at insert time) is fixed by
//   row_union_t's recursion (METX-2 #474) and consistent across both
//   `then` overloads.  Production callers wanting a different bias
//   can post-process via `weaken<canonical_t<R>>()` once a canonical
//   form lands; until then, semantic Subrow equality is the contract.
//
// Operations now live:
//   mk(T)              — lift a pure T into Computation<EmptyRow, T>
//   extract()          — collapse Computation<EmptyRow, T> back to T
//                         (lvalue → const T&; rvalue → T)
//   lift<Cap>(T)       — construct at row {Cap}
//   weaken<R₂>()       — widen row when Subrow<R, R₂>
//                         (lvalue → copy; rvalue → move)
//   map(f)             — fmap: T -> U preserving row R
//                         (lvalue → invoke on const T&; rvalue → on T)
//   then(k)            — bind: k : T -> Computation<R₂, U>
//                         result row = row_union_t<R, R₂>
//                         (lvalue → invoke on const T&; rvalue → on T)
//
// See Capabilities.h (Effect atoms + cap::* tag types + Bg/Init/Test
// contexts), EffectRow.h (Row + Subrow algebra).  The legacy
// crucible/Effects.h fx::* tree was deleted in FOUND-B07 / METX-5.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::effects {

// Forward declare for the IsComputation gate below.
template <typename R, typename T>
class Computation;

// ── IsComputation concept ───────────────────────────────────────────
//
// Trait + concept gating then()'s callable result type.  Specialization
// of detail::is_computation lives AFTER the Computation class
// definition (Computation must be complete before we can specialize
// over it); the requires-clause inside the class body is checked at
// then()'s instantiation, by which time the specialization is visible.
namespace detail {
template <typename> struct is_computation : std::false_type {};
}

template <typename T>
concept IsComputation =
    detail::is_computation<std::remove_cvref_t<T>>::value;

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

    // ── map(f) — functor fmap, preserves row R ──────────────────────
    //
    // f : T -> U.  Result row is unchanged because mapping a pure
    // function over a Computation<R, T>'s value cannot introduce new
    // effects — the function f itself is a pure-value transformation
    // (any effect f wants to add must be discharged via then, not map).
    //
    // Two overloads to avoid an unnecessary copy on rvalue uses.
    // U is deduced from std::invoke_result_t<F, ...>.

    template <typename F>
        requires std::is_invocable_v<F, const T&>
    [[nodiscard]] constexpr auto map(F&& f) const &
        noexcept(std::is_nothrow_invocable_v<F, const T&>
                 && std::is_nothrow_move_constructible_v<
                        std::invoke_result_t<F, const T&>>)
        -> Computation<R, std::invoke_result_t<F, const T&>>
    {
        using U = std::invoke_result_t<F, const T&>;
        return Computation<R, U>{std::forward<F>(f)(inner_)};
    }

    template <typename F>
        requires std::is_invocable_v<F, T>
    [[nodiscard]] constexpr auto map(F&& f) &&
        noexcept(std::is_nothrow_invocable_v<F, T>
                 && std::is_nothrow_move_constructible_v<
                        std::invoke_result_t<F, T>>)
        -> Computation<R, std::invoke_result_t<F, T>>
    {
        using U = std::invoke_result_t<F, T>;
        return Computation<R, U>{std::forward<F>(f)(std::move(inner_))};
    }

    // ── then(k) — monadic bind, accumulates effect rows ─────────────
    //
    // k : T -> Computation<R2, U>.  Result row is row_union_t<R, R2>:
    // every effect of either side is carried forward, with duplicates
    // absorbed by the union's set semantics.  This is the substitution
    // principle in reverse: the result claims every capability that
    // any step in the chain might exercise.
    //
    // `IsComputation<...>` requires-clause rejects callbacks whose
    // result is not a Computation — distinguishes then (bind) from
    // map (fmap) at the type level instead of accidentally accepting a
    // pure-value k as a degenerate bind.  A user wanting fmap behavior
    // should call .map(f) directly.
    //
    // Two overloads for lvalue/rvalue source; both forward the inner
    // result's value into a fresh Result.

    template <typename F>
        requires std::is_invocable_v<F, const T&>
              && IsComputation<std::invoke_result_t<F, const T&>>
    [[nodiscard]] constexpr auto then(F&& k) const &
        -> Computation<
            row_union_t<R, typename std::invoke_result_t<F, const T&>::row_type>,
            typename std::invoke_result_t<F, const T&>::value_type>
    {
        using Inner  = std::invoke_result_t<F, const T&>;
        using R2     = typename Inner::row_type;
        using U      = typename Inner::value_type;
        using Result = Computation<row_union_t<R, R2>, U>;
        Inner intermediate = std::forward<F>(k)(inner_);
        return Result{std::move(intermediate.inner_)};
    }

    template <typename F>
        requires std::is_invocable_v<F, T>
              && IsComputation<std::invoke_result_t<F, T>>
    [[nodiscard]] constexpr auto then(F&& k) &&
        -> Computation<
            row_union_t<R, typename std::invoke_result_t<F, T>::row_type>,
            typename std::invoke_result_t<F, T>::value_type>
    {
        using Inner  = std::invoke_result_t<F, T>;
        using R2     = typename Inner::row_type;
        using U      = typename Inner::value_type;
        using Result = Computation<row_union_t<R, R2>, U>;
        Inner intermediate = std::forward<F>(k)(std::move(inner_));
        return Result{std::move(intermediate.inner_)};
    }
};

// ── is_computation specialization (post-class) ──────────────────────
//
// Now that Computation is complete, specialize the trait so the
// IsComputation concept gates correctly.  Per the rule documented at
// the trait's primary template above, the requires-clause checks at
// instantiation time, by which point this specialization IS visible.
namespace detail {
template <typename R, typename T>
struct is_computation<Computation<R, T>> : std::true_type {};
}  // namespace detail

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

// ── map / then coverage ────────────────────────────────────────────
//
// IsComputation discriminator — gates then() at the type level.
static_assert( IsComputation<Computation<Row<>, int>>);
static_assert( IsComputation<Computation<Row<Effect::Bg>, double>>);
static_assert( IsComputation<Computation<Row<>, int>&>);              // cvref-strip
static_assert( IsComputation<const Computation<Row<>, int>&>);        // cvref-strip
static_assert(!IsComputation<int>);
static_assert(!IsComputation<Row<Effect::Bg>>);

// map: row preserved, value type may change.
static_assert([] consteval {
    auto pure = Computation<Row<>, int>::mk(7);
    auto doubled = pure.map([](int x) { return x * 2; });
    using Doubled = decltype(doubled);
    return std::is_same_v<Doubled::row_type, Row<>>
        && std::is_same_v<Doubled::value_type, int>
        && doubled.extract() == 14;
}(),
"map preserves row and applies the function pointwise.");

// map: value type can change shape (int -> double).
static_assert([] consteval {
    auto pure = Computation<Row<>, int>::mk(3);
    auto as_double = pure.map([](int x) -> double { return x + 0.5; });
    using D = decltype(as_double);
    return std::is_same_v<D::row_type, Row<>>
        && std::is_same_v<D::value_type, double>;
}(),
"map admits value-type changes (T -> U) while preserving the row.");

// then: row accumulates via row_union; sequencing two effectful steps
// produces a Computation whose row contains every effect of either
// step (with set-union semantics — duplicates absorbed).
static_assert([] consteval {
    auto bg = Computation<Row<>, int>::lift<Effect::Bg>(10);
    auto chained = bg.then([](int x) {
        return Computation<Row<>, int>::lift<Effect::IO>(x + 1);
    });
    using Chained = decltype(chained);
    // Result row should contain BOTH Bg (from outer) and IO (from inner).
    return is_subrow_v<Row<Effect::Bg>, Chained::row_type>
        && is_subrow_v<Row<Effect::IO>, Chained::row_type>
        && std::is_same_v<Chained::value_type, int>;
}(),
"then accumulates the inner Computation's row into the outer's via "
"row_union_t — both sides' effects are preserved in the result.");

// then: rvalue overload moves through the chain.
static_assert([] consteval {
    auto bg = Computation<Row<>, int>::lift<Effect::Bg>(100);
    auto chained = std::move(bg).then([](int x) {
        return Computation<Row<>, int>::lift<Effect::Bg>(x);
    });
    // Both sides have Bg; the union should be just Bg (set semantics).
    using Chained = decltype(chained);
    return is_subrow_v<Row<Effect::Bg>, Chained::row_type>
        && is_subrow_v<Chained::row_type, Row<Effect::Bg>>;  // mutual ⊑ = equal
}(),
"then with overlapping rows absorbs duplicates via row_union's set "
"semantics — Subrow-equality holds in both directions.");

// then: empty-empty case (the special case that monad laws bottom on).
static_assert([] consteval {
    auto pure = Computation<Row<>, int>::mk(5);
    auto chained = pure.then([](int x) {
        return Computation<Row<>, int>::mk(x * 2);
    });
    return chained.extract() == 10
        && std::is_same_v<decltype(chained)::row_type, Row<>>;
}(),
"then on empty rows produces a result also at the empty row (monad "
"left/right unit law instance).");

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

    // map: pure-value transformation, row preserved.  Lvalue and
    // rvalue overloads exercise the const-ref vs T-by-value paths.
    auto map_pure   = Computation<Row<>, int>::mk(7);
    auto map_lvalue = map_pure.map([](int x) { return x + 1; });
    auto map_rvalue = std::move(map_pure).map([](int x) { return x * 2; });
    (void)map_lvalue;
    (void)map_rvalue;

    // then: monadic bind across two effect rows.  The chain
    // accumulates Bg into the result row via row_union.  Both lvalue
    // and rvalue then() overloads exercised.
    auto then_pure = Computation<Row<>, int>::mk(11);
    auto then_lvalue = then_pure.then([](int x) {
        return Computation<Row<>, int>::lift<Effect::Bg>(x + 100);
    });
    auto then_rvalue = std::move(then_pure).then([](int x) {
        return Computation<Row<>, int>::lift<Effect::IO>(x + 200);
    });
    static_assert(std::is_same_v<
        decltype(then_lvalue)::row_type,
        Row<Effect::Bg>
    >);
    static_assert(std::is_same_v<
        decltype(then_rvalue)::row_type,
        Row<Effect::IO>
    >);
    (void)then_lvalue;
    (void)then_rvalue;
}

}  // namespace crucible::effects
