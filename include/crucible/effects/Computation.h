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
//   Runtime cost:   zero.  Storage is the H03 substrate
//                   `ComputationGraded<R, T>` whose grade slot
//                   EBO-collapses to 0 bytes; with
//                   `[[no_unique_address]]` on impl_ this carrier is
//                   exactly sizeof(T) regardless of how many atoms R
//                   carries.
//
// ── FOUND-H04 façade migration (substrate-backed) ───────────────────
//
// The Computation class IS now a thin façade over ComputationGraded
// (`include/crucible/effects/ComputationGraded.h`).  The public API
// surface (mk / extract / lift<Cap> / weaken<R2> / map / then) is
// preserved at the source level — every prior caller of these
// methods continues to work without source changes.
//
// Internally, the storage is `[[no_unique_address]] graded_type
// impl_{};` where `graded_type = ComputationGraded<R, T>`.  Member
// functions reroute through `impl_.peek()` / `std::move(impl_).
// consume()` for value access, and construct new Computation
// instances with new R/T template parameters for type-level row
// arithmetic (the parts the substrate cannot express because
// changing R changes the type).
//
// Cross-specialization access (then's body must read the inner T
// out of a different `Computation<R2, U>`) uses the standard
// monadic-carrier friend-template idiom:
//
//   template <typename, typename> friend class Computation;
//
// ── Substrate escape hatch ──────────────────────────────────────────
//
// Code that wants the substrate view (e.g., to compute `lattice` /
// `modality` / `grade()` for diagnostics or cache keying) reads
// `Computation<R, T>::graded_type` and the new `.graded()` accessor,
// which forwards to the underlying ComputationGraded.  This is the
// non-breaking on-ramp for downstream FOUND-I-series code that
// targets the substrate uniformly.
//
// Operations:
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
//   graded()           — substrate accessor (FOUND-H04 escape hatch)
//                         lvalue → const graded_type&; rvalue → graded_type
//
// See Capabilities.h (Effect atoms + cap::* tag types + Bg/Init/Test
// contexts), EffectRow.h (Row + Subrow algebra), EffectRowLattice.h
// (the value-level lattice + At<> singleton sub-lattice), and
// ComputationGraded.h (the substrate alias H04 wraps).

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/ComputationGraded.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>

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
    // Cross-specialization friendship — `then`'s body needs to read
    // the inner T of a Computation<R2, U> returned from the bind
    // callback.  Standard monadic-carrier idiom; no broader access.
    template <typename, typename> friend class Computation;

public:
    // ── Public type aliases ─────────────────────────────────────────
    using row_type    = R;
    using value_type  = T;

    // The substrate identity (FOUND-H04).  Downstream code targeting
    // the Graded substrate uniformly (FOUND-I cache key federation,
    // FOUND-J row-typed Forge IR) reaches it through this typedef
    // and the `graded()` accessor.
    using graded_type = ComputationGraded<R, T>;

    static constexpr std::size_t row_size = row_size_v<R>;

private:
    // ── Layout: substrate-backed (FOUND-H04) ────────────────────────
    //
    // Storage IS the substrate.  graded_type's grade slot
    // EBO-collapses to 0 bytes (At<Es...>::element_type is empty by
    // FOUND-H01-AUDIT-1), so sizeof(Computation<R, T>) ==
    // sizeof(graded_type) == sizeof(T) — the layout invariant macro
    // CRUCIBLE_COMPUTATION_LAYOUT_INVARIANT below pins this.
    //
    // NSDMI per InitSafe: graded_type's own NSDMI initializes its
    // inner_{} and grade_{} members; we inherit that here.
    [[no_unique_address]] graded_type impl_{};

public:
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
    //
    // Forwards into the substrate's two-arg ctor with a default-
    // constructed grade (the empty-struct singleton inhabitant of
    // At<Es...>::element_type).
    explicit constexpr Computation(T x)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(x), typename graded_type::grade_type{}} {}

    // ── Public operations (METX-1 #473 bodies, H04 substrate-routed) ─
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
    // rvalue returns by value (move out of impl_).
    [[nodiscard]] constexpr const T& extract() const & noexcept
        requires (row_size_v<R> == 0)
    {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T extract() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
        requires (row_size_v<R> == 0)
    {
        return std::move(impl_).consume();
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
    //
    // Type-level row widening (R → R2 in the result type) is a
    // SEPARATE operation from the substrate's runtime weaken (which
    // operates on the singleton grade and is degenerate).  This
    // member function performs the type-level widening; the substrate
    // grade carries no information here.
    template <typename R2>
        requires Subrow<R, R2>
    [[nodiscard]] constexpr Computation<R2, T> weaken() const &
        noexcept(std::is_nothrow_copy_constructible_v<T>)
    {
        return Computation<R2, T>{impl_.peek()};
    }

    template <typename R2>
        requires Subrow<R, R2>
    [[nodiscard]] constexpr Computation<R2, T> weaken() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Computation<R2, T>{std::move(impl_).consume()};
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
        return Computation<R, U>{std::forward<F>(f)(impl_.peek())};
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
        return Computation<R, U>{
            std::forward<F>(f)(std::move(impl_).consume())};
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
    // result's value into a fresh Result.  The cross-specialization
    // friend template grants then access to `intermediate.impl_` —
    // no public-field exposure required.

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
        Inner intermediate = std::forward<F>(k)(impl_.peek());
        return Result{std::move(intermediate.impl_).consume()};
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
        Inner intermediate = std::forward<F>(k)(std::move(impl_).consume());
        return Result{std::move(intermediate.impl_).consume()};
    }

    // ── graded() — substrate-view escape hatch (FOUND-H04) ──────────
    //
    // Exposes the underlying ComputationGraded<R, T> for downstream
    // code that wants the substrate's uniform diagnostic surface
    // (modality_name / lattice_name / grade), cache-key composition
    // (FOUND-I-series row_hash), or any future Graded-targeting
    // operation.  Two overloads to allow zero-copy borrow on lvalue
    // and move-out on rvalue source.
    [[nodiscard]] constexpr const graded_type& graded() const & noexcept {
        return impl_;
    }

    [[nodiscard]] constexpr graded_type graded() &&
        noexcept(std::is_nothrow_move_constructible_v<graded_type>)
    {
        return std::move(impl_);
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

// FOUND-H04: substrate identity reachable through graded_type alias.
static_assert(std::is_same_v<C_empty::graded_type,
                             ComputationGraded<Row<>, EmptyValue>>);
static_assert(std::is_same_v<C_eight_byte::graded_type,
                             ComputationGraded<Row<Effect::Bg>, EightByteValue>>);

// Default-constructible.
static_assert(std::is_default_constructible_v<C_empty>);
static_assert(std::is_default_constructible_v<C_one_byte>);
static_assert(std::is_default_constructible_v<C_eight_byte>);

// Layout — the row is type-level only, no runtime cost.  H04
// substrate-backed storage preserves this exactly via Graded's EBO.
static_assert(sizeof(C_empty)      == 1);
static_assert(sizeof(C_one_byte)   == sizeof(OneByteValue));
static_assert(sizeof(C_eight_byte) == sizeof(EightByteValue));

// FOUND-H04-AUDIT-2: layout parity with the substrate.  sizeof was
// already checked above; alignof + trivially_copyable_v + standard-
// layout parity lock the wrapping further so that ANY divergence
// from the substrate's layout — including padding insertion or a
// codegen-visible attribute change — is caught at compile time.
static_assert(alignof(C_empty)      == alignof(typename C_empty::graded_type));
static_assert(alignof(C_one_byte)   == alignof(typename C_one_byte::graded_type));
static_assert(alignof(C_eight_byte) == alignof(typename C_eight_byte::graded_type));

// Trivial copyability is preserved when T is itself trivially
// copyable — graded_type stores T by value in regime-1 EBO, so the
// composition is trivially copyable iff T is.  Locks the property
// the substrate already promises.
static_assert(std::is_trivially_copyable_v<C_empty>
              == std::is_trivially_copyable_v<typename C_empty::graded_type>);
static_assert(std::is_trivially_copyable_v<C_one_byte>
              == std::is_trivially_copyable_v<typename C_one_byte::graded_type>);
static_assert(std::is_trivially_copyable_v<C_eight_byte>
              == std::is_trivially_copyable_v<typename C_eight_byte::graded_type>);

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
//
// Final value access uses the H04 substrate accessor since `widest`
// has a non-empty row (cannot use extract() — gated off).  Equivalent
// observation: the wrapped value survives every type-level widening.
static_assert([] consteval {
    auto bg     = Computation<Row<>, int>::lift<Effect::Bg>(13);
    auto wider  = bg.template weaken<Row<Effect::Bg, Effect::Alloc>>();
    auto widest = wider.template weaken<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
    return widest.graded().peek() == 13;
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

// ── FOUND-H04 substrate accessor coverage ──────────────────────────
//
// graded() returns the underlying ComputationGraded.  Drive both
// lvalue and rvalue overloads + verify the type identity.

static_assert([] consteval {
    auto pure = Computation<Row<>, int>::mk(99);
    auto const& g = pure.graded();
    using G = std::remove_cvref_t<decltype(g)>;
    return std::is_same_v<G, ComputationGraded<Row<>, int>>
        && g.peek() == 99;
}(),
"graded() lvalue overload exposes the substrate view at the correct "
"specialization.");

static_assert([] consteval {
    auto pure = Computation<Row<Effect::Bg>, int>{77};
    auto g    = std::move(pure).graded();   // rvalue overload
    using G = decltype(g);
    return std::is_same_v<G, ComputationGraded<Row<Effect::Bg>, int>>
        && g.peek() == 77;
}(),
"graded() rvalue overload moves the substrate out at the correct "
"specialization.");

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

    // FOUND-H04 substrate accessor — drive both overloads at runtime.
    auto graded_lvalue_owner = Computation<Row<Effect::Bg>, int>{555};
    auto const& g_view = graded_lvalue_owner.graded();   // const& overload
    [[maybe_unused]] int peeked = g_view.peek();

    auto graded_rvalue_owner = Computation<Row<Effect::Bg>, int>{666};
    auto g_moved = std::move(graded_rvalue_owner).graded();   // && overload
    [[maybe_unused]] int peeked_moved = g_moved.peek();
}

}  // namespace crucible::effects
