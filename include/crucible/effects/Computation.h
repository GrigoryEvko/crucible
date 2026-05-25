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
#include <crucible/effects/ExecCtx.h>

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

// FIXY-FOUND-107: extract_admits_payload — gate to reject extract on
// engagement-laundered nested Computations.  Specialized for
// Computation<R, T> AFTER the class definition; see the post-class
// specialization block paired with `is_computation<Computation<R, T>>`.
//
// For non-Computation T (e.g., int, string, struct): always true —
// extract just unwraps the value, nothing to launder.
//
// For T = Computation<R', U>: requires R' to be empty AND U to also
// admit extract.  This recursively forbids
//     Computation<Row<>, Computation<Row<Bg>, U>>::extract()
// where the outer Row<> launders the inner Row<Bg>'s engagement.
template <typename> struct extract_admits_payload : std::true_type {};
template <typename T>
inline constexpr bool extract_admits_payload_v =
    extract_admits_payload<T>::value;
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

    // `mint_computation` — §XXI Universal Mint Pattern alias of `mk`
    // (FIXY-FOUND-077).  Same body, same `requires (row_size_v<R> == 0)`
    // cross-tier admission gate; exists so `grep -r "mint_"` finds every
    // authorization point in the codebase.  Production call sites SHOULD
    // prefer this spelling; the historical `mk` survives for backward
    // compatibility (27+ established call sites) and for visual symmetry
    // with the Haskell-style `pure`/`return` lift it models.  Both names
    // route through the same explicit value-ctor; binary-identical
    // codegen at -O3 (zero-overhead alias).
    //
    // §XXI shape audit: this is a token-mint (no Ctx param) because the
    // pure-row lift derives authority from the empty-row R constraint
    // itself — there's nothing left to gate at the ctx level.  Compare
    // with `mint_computation_in_ctx<Cap, Ctx>` (below) which IS Ctx-bound.
    [[nodiscard]] static constexpr Computation mint_computation(T x)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        requires (row_size_v<R> == 0)
    {
        return Computation{std::move(x)};
    }

    // `extract` — unwrap a pure value out of an empty-row Computation.
    // Two overloads: lvalue returns const-ref (no copy on inspection),
    // rvalue returns by value (move out of impl_).
    //
    // FIXY-FOUND-107: the `extract_admits_payload_v<T>` constraint
    // forbids engagement laundering through nested-Computation
    // payloads.  Concretely, the construction
    //     Computation<Row<>, Computation<Row<Bg>, int>>
    // wraps a Bg-engaged inner Computation inside a pure-looking
    // outer.  Without the gate, extract() on the outer would return
    // the engaged inner — operations between the wrap site and the
    // extract site would have seen `Computation<Row<>, T>` and
    // believed they were operating on pure data, when in fact T is
    // itself effectful.  The Bg effect's audit trail through those
    // operations would be lost.
    //
    // The constraint recursively rejects: T = Computation<R', U>
    // requires R' to be empty AND U to also admit extract.
    [[nodiscard]] constexpr const T& extract() const & noexcept
        requires (row_size_v<R> == 0)
              && detail::extract_admits_payload_v<T>
    {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T extract() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
        requires (row_size_v<R> == 0)
              && detail::extract_admits_payload_v<T>
    {
        return std::move(impl_).consume();
    }

    // `lift<Cap>` — construct a Computation at row {Cap} from a raw T.
    // Static (the current row R is irrelevant to the result type).
    // Explicit `IsEffect<Cap>` requires-clause rejects template typos
    // at substitution time, not at use site.
    //
    // ── FIXY-FOUND-014 band-3 closure (2026-05-25) ────────────────────
    //
    // `lift<Cap>(x)` is the SECOND of two forging surfaces V-219
    // identified.  The FIRST (weaken-from-empty) is closed at
    // `weaken<R2>` by the `(row_size_v<R> > 0 || row_size_v<R2> == 0)`
    // guard.  THIS surface remains OPEN at the type level:
    //
    //     Computation<Row<>, int>::lift<Effect::IO>(42)
    //   // ↑ compiles, yields Computation<Row<IO>, int>
    //   // ↑ NO PROOF the production of `42` exercised IO
    //
    // The caller declares an effect at the lift site; the type system
    // trusts the declaration without provenance.  This is symmetric
    // with `then`'s callback-honesty hole (FIXY-FOUND-108, closed by
    // banning engaged-Computation payloads via `extract_admits_payload_v`)
    // and `extract`'s laundering hole (FIXY-FOUND-107, closed by the
    // same trait).  Lift's hole is structural — there is NO type-level
    // axis available on a raw T that proves "this value's production
    // exercised Cap."  The honesty discipline is reviewer-policed.
    //
    // The companion concept `fixy::effect::RowEngagementWitnessed<C>`
    // surfaces the resulting type-level engagement claim at band-3
    // (see fixy/Effect.h:172).  When a future audit lands the
    // RIGOROUS closure shape — a ctx-witnessed `lift_in<Cap>(Ctx const&, T)`
    // overload that gates on `row_contains_v<Ctx::row_type, Cap>` —
    // the unwitnessed form here moves to a deprecation shim, the
    // sibling concept `RowEngagementProvenancePinned<C>` lands per
    // fixy/Effect.h:139-144, and production-Forge call sites migrate.
    //
    // Cycle-budget note: the witnessed-form overload requires pulling
    // ExecCtx.h into Computation.h's hot-path include set and
    // updating the 10 in-file smoke-test call sites.  That is the
    // follow-up ship (next FOUND-014 cycle).  Today's ship is the
    // loud audit-conclusion documentation here + the static_assert
    // sentinel below proving the forge IS structurally possible —
    // i.e., the open-hole status is structurally documented at the
    // call-site source, not just in the ticket text.
    template <Effect Cap>
        requires IsEffect<Cap>
    [[nodiscard]] static constexpr auto lift(T x)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        -> Computation<Row<Cap>, T>
    {
        return Computation<Row<Cap>, T>{std::move(x)};
    }

    // `lift_in<Cap, Ctx>` — FIXY-FOUND-014 rigorous closure.
    //
    // Ctx-witnessed companion to `lift<Cap>(x)`.  The constraint
    // `row_contains_v<Ctx::row_type, Cap>` is the type-level proof
    // that the caller's execution context legitimately permits the
    // claimed effect — the forge surface that the unwitnessed
    // `lift<Cap>(x)` left open is closed here at the type-system
    // level.  Production call sites that legitimately attach a row
    // at construction time MUST migrate to this form; the bare
    // `lift<Cap>(x)` survives ONLY for the in-file smoke tests
    // (where the ctx infrastructure is intentionally absent) and
    // for the FIXY-FOUND-014 forge-surface demonstration sentinel
    // below.
    //
    // Gate composition (left-to-right short-circuit):
    //   * IsEffect<Cap>                                — Cap is a valid Effect atom
    //   * IsExecCtx<Ctx>                               — Ctx is a well-formed ExecCtx<...>
    //   * row_contains_v<Ctx::row_type, Cap>           — Ctx's row carries Cap
    //
    // The first two are sanity gates (typo rejection); the third
    // IS the rigorous closure.  A caller in `HotFgCtx{}` (row =
    // `Row<>`) CANNOT mint a Bg-claimed Computation via lift_in:
    // the third predicate evaluates false at substitution, the
    // function template is constrained-out of overload resolution,
    // and the call site fails to find a candidate.  A caller in
    // `BgDrainCtx{}` (row = `Row<Bg, Alloc>`) can.
    //
    // Symmetric with `mint_cap<E>(source)` (FOUND-105) — passkey-
    // free because Computation construction doesn't need an
    // unforgeable token; the type-level gate IS the unforgeable
    // proof.  The rvalue-ref-qualified consume discipline applies
    // to the RETURNED Computation, not to lift_in itself.
    //
    // Hot-path note: `Ctx const&` parameter is EBO-collapsible if
    // Ctx is empty (all canonical ExecCtx aliases are 1-byte empty
    // types under [[no_unique_address]]).  Zero-cost dispatch at
    // -O3 — the parameter is unused at runtime, only its TYPE
    // matters at substitution.
    template <Effect Cap, class Ctx>
        requires IsEffect<Cap>
              && ::crucible::effects::IsExecCtx<Ctx>
              && row_contains_v<typename std::remove_cvref_t<Ctx>::row_type, Cap>
    [[nodiscard]] static constexpr auto lift_in(Ctx const&, T x)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        -> Computation<Row<Cap>, T>
    {
        return Computation<Row<Cap>, T>{std::move(x)};
    }

    // `mint_computation_in_ctx<Cap, Ctx>` — §XXI Universal Mint Pattern
    // alias of `lift_in` (FIXY-FOUND-077).  Canonical §XXI Ctx-bound
    // shape: `requires CtxFitsX<X, Ctx>` (here: `row_contains_v<Ctx::
    // row_type, Cap>`).  Exposes the ctx-bound forging surface to
    // `grep "mint_"`; production call sites SHOULD prefer this spelling.
    // The historical `lift_in` survives for backward compatibility and
    // for paired use with the unwitnessed `lift<Cap>` form (which lives
    // outside the mint surface because no ctx fit-check exists).
    //
    // §XXI shape: Ctx-bound mint (first parameter is `Ctx const&`),
    // returns concrete `Computation<Row<Cap>, T>`, single requires-clause
    // is the ctx-fit check, [[nodiscard]] + constexpr + noexcept.  All
    // boxes ticked from the canonical mints table.  Binary-identical
    // codegen at -O3 with `lift_in` (zero-overhead alias).
    template <Effect Cap, class Ctx>
        requires IsEffect<Cap>
              && ::crucible::effects::IsExecCtx<Ctx>
              && row_contains_v<typename std::remove_cvref_t<Ctx>::row_type, Cap>
    [[nodiscard]] static constexpr auto mint_computation_in_ctx(Ctx const&, T x)
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
    //
    // ── FIXY-V-219 row-engagement guard (Agent 8 Bug 7) ─────────────
    //
    // Subrow<Row<>, R2> is TRUE for every R2 (Row<> is the bottom of
    // the row lattice).  Without an extra guard, the chain
    //
    //     Computation<Row<>, T>::mk(x).weaken<Row<Effect::Alloc>>()
    //
    // synthesises a `Computation<Row<Effect::Alloc>, T>` whose row
    // claim is structurally honest (substitution principle) but
    // discipline-unfriendly: a reviewer reading the result type
    // expects the body to demonstrate Alloc, and weaken-from-pure
    // HIDES that the body never engaged Alloc.  V-219 closes the
    // hole by adding `(row_size_v<R> > 0 || row_size_v<R2> == 0)` —
    // weakening is allowed only when the SOURCE row is already
    // engaged (widening within non-empty rows) OR when both sides
    // stay empty (degenerate Row<> → Row<> identity).  Production
    // call sites that legitimately want to attach a row at pure-
    // construction time must use `lift<Cap>(x)` (which engages the
    // row at construction) or a stage mint (which announces the row
    // at the stage signature).  See the `fixy::effect` namespace's
    // `RowEngagementWitnessed<C>` concept for the band-3 stance
    // that surfaces this guarantee at call sites.
    // FIXY-FOUND-106: lvalue overload requires `std::is_copy_constructible_v<T>`.
    // Pre-fix, the body did `Computation<R2, T>{impl_.peek()}` where
    // `peek()` returns `T const&` and the value-ctor copies T.  For
    // move-only T (e.g., `Computation<R, std::unique_ptr<X>>` or any
    // T wrapping `Linear<X>`), this fails inside the body — a hard
    // template-instantiation error far from the call site, with the
    // diagnostic pointing at the impl_ initialization rather than at
    // the user's weaken<R2>() call.
    //
    // The SFINAE gate moves the failure to OVERLOAD RESOLUTION: when
    // T is move-only, the lvalue overload is silently absent and only
    // the rvalue overload (`weaken() &&`) is available.  A user
    // calling `c.weaken<R2>()` on an lvalue Computation gets a clear
    // diagnostic ("no matching member function — constraints not
    // satisfied: std::is_copy_constructible_v<T>") + the rvalue
    // overload pointed out as the working alternative.
    //
    // The rvalue overload is unconditionally available — it consumes
    // the Linear<T> via std::move, which works for move-only T by
    // construction.
    template <typename R2>
        requires Subrow<R, R2>
              && (row_size_v<R> > 0 || row_size_v<R2> == 0)
              && std::is_copy_constructible_v<T>
    [[nodiscard]] constexpr Computation<R2, T> weaken() const &
        noexcept(std::is_nothrow_copy_constructible_v<T>)
    {
        return Computation<R2, T>{impl_.peek()};
    }

    template <typename R2>
        requires Subrow<R, R2>
              && (row_size_v<R> > 0 || row_size_v<R2> == 0)
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
    // FIXY-FOUND-108: callback honesty.  The TYPE system trusts the
    // callback's DECLARED return row R2 — it cannot statically prove
    // the callback body's effects fit inside R2.  FIXY-FOUND-014 lets
    // a callback forge a smaller R2 via Computation::lift<Cap>; that
    // forging surface is closed separately.  Here we close the SECOND
    // laundering shape, symmetric with FIXY-FOUND-107's extract gate:
    //
    //   k : T -> Computation<R2, Computation<R3, V>>
    //
    // The callback declares row R2, but the returned VALUE is itself
    // an engaged Computation carrying R3.  After then accumulates R
    // ∪ R2, the inner R3 is invisible in the resulting type — every
    // downstream operation sees Computation<R∪R2, Computation<R3, V>>
    // and the inner row's audit trail is laundered.
    //
    // The `extract_admits_payload_v<U>` constraint (shared with
    // extract's gate) recursively rejects callbacks whose value_type
    // is an engaged Computation.  Legitimate monadic chaining is
    // unaffected: nest Computations via successive .then() calls, not
    // by returning a nested Computation value from a single callback.
    //
    // Two overloads for lvalue/rvalue source; both forward the inner
    // result's value into a fresh Result.  The cross-specialization
    // friend template grants then access to `intermediate.impl_` —
    // no public-field exposure required.

    template <typename F>
        requires std::is_invocable_v<F, const T&>
              && IsComputation<std::invoke_result_t<F, const T&>>
              && detail::extract_admits_payload_v<
                     typename std::invoke_result_t<F, const T&>::value_type>
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
              && detail::extract_admits_payload_v<
                     typename std::invoke_result_t<F, T>::value_type>
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

// FIXY-FOUND-107: recursive payload-admission specialization.
// Nested Computation<R, U> as payload is rejected by extract iff R
// is non-empty (engaged) OR U itself recursively fails the check.
template <typename R, typename U>
struct extract_admits_payload<Computation<R, U>>
    : std::bool_constant<(row_size_v<R> == 0)
                       && extract_admits_payload<U>::value> {};
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
// FIXY-V-219: source row is non-empty (Row<Effect::Bg>) so the
// V-219 row-engagement guard (row_size_v<R> > 0) is satisfied; the
// noexcept-inference check focuses on the move-vs-copy choice, not
// on whether the source-row gate fires.
static_assert(
    noexcept(std::declval<Computation<Row<Effect::Bg>, int>>().template weaken<Row<Effect::Bg, Effect::IO>>()),
    "Computation<R, int>::weaken() && must be noexcept for trivially-"
    "move-constructible payloads.");

// ── FIXY-FOUND-106 regression: copyable payload weaken both overloads ──
//
// The SFINAE gate `is_copy_constructible_v<T>` on the lvalue weaken
// MUST NOT over-restrict the common case (T copy-constructible).
// Witness: `int` is copy-constructible and BOTH lvalue and rvalue
// weaken must work.

static_assert(requires(Computation<Row<Effect::Bg>, int> const& c) {
    c.template weaken<Row<Effect::Bg, Effect::IO>>();
}, "FIXY-FOUND-106 regression: lvalue weaken must remain available "
   "for copy-constructible payloads.");

static_assert(requires(Computation<Row<Effect::Bg>, int>&& c) {
    std::move(c).template weaken<Row<Effect::Bg, Effect::IO>>();
}, "FIXY-FOUND-106 regression: rvalue weaken must remain available "
   "for copy-constructible payloads.");

// The structural claim — "lvalue weaken's requires-clause includes
// is_copy_constructible_v<T>" — is enforced by the requires-clause
// itself.  An explicit `is_copy_constructible_v<int> == true` witness
// covers the positive case; the full negative case (move-only T)
// would instantiate Computation<R, MoveOnly> which surfaces a
// Linear<MoveOnly>-related copy-required code path in some non-
// template member.  The full neg-compile fixture lives under
// test/safety_neg/ instead (deferred HS14 follow-up — beyond cycle-
// ship scope).
static_assert(std::is_copy_constructible_v<int>,
    "FIXY-FOUND-106 sanity: int is copy-constructible (positive-case "
    "witness for the requires-clause).");

// ── FIXY-FOUND-106-AUDIT: trait-level pin for the negative case ────
//
// The full negative-compile fixture (move-only T → lvalue weaken
// rejected by concept-failure with clean diag) is deferred to
// test/safety_neg/ due to a Linear<MoveOnly>-instantiation issue
// orthogonal to the SFINAE gate.  This trait-level pin verifies the
// LOAD-BEARING claim: `std::is_copy_constructible_v` correctly
// returns false for a type with a deleted copy ctor.  Without that
// trait behaviour, the lvalue weaken's requires-clause is structurally
// meaningless — the SFINAE gate would not actually gate anything.
//
// This pin doesn't instantiate Computation<R, MoveOnlyProbe>; it
// just exercises the trait the requires-clause depends on.
namespace fixy_found_106_trait_pin {

struct MoveOnlyProbe {
    constexpr MoveOnlyProbe() noexcept = default;
    constexpr MoveOnlyProbe(MoveOnlyProbe&&) noexcept = default;
    MoveOnlyProbe(MoveOnlyProbe const&) = delete;
    MoveOnlyProbe& operator=(MoveOnlyProbe&&) noexcept = default;
    MoveOnlyProbe& operator=(MoveOnlyProbe const&) = delete;
    ~MoveOnlyProbe() = default;
};

static_assert(!std::is_copy_constructible_v<MoveOnlyProbe>,
    "FIXY-FOUND-106-AUDIT: the SFINAE gate depends on "
    "std::is_copy_constructible_v returning false for deleted-copy "
    "types.  If this assertion ever fires, the lvalue weaken overload "
    "stops gating move-only payloads and silently re-admits the bug.");

static_assert(std::is_move_constructible_v<MoveOnlyProbe>,
    "FIXY-FOUND-106-AUDIT: rvalue weaken on move-only T relies on T "
    "being move-constructible.  If this assertion fires, the rvalue "
    "overload's `std::move(impl_).consume()` path itself stops working.");

}  // namespace fixy_found_106_trait_pin

// ── FIXY-FOUND-107: extract engagement-laundering gate ─────────────
//
// Trait-level pins for the recursive `extract_admits_payload_v<T>`
// gate.  Non-Computation T always admits; Computation<R, U> admits
// iff R is empty AND U recursively admits.

namespace fixy_found_107 {

// Plain payloads admit.
static_assert(detail::extract_admits_payload_v<int>);
static_assert(detail::extract_admits_payload_v<double>);

// Pure Computation payload admits (Row<>-wrapped Row<>-wrapped T).
static_assert(detail::extract_admits_payload_v<
    Computation<Row<>, int>>);

// Engaged Computation payload REJECTED — this is the laundering case.
static_assert(!detail::extract_admits_payload_v<
    Computation<Row<Effect::Bg>, int>>);
static_assert(!detail::extract_admits_payload_v<
    Computation<Row<Effect::Alloc, Effect::IO>, int>>);

// Recursive case: Computation<Row<>, Computation<Row<Bg>, T>> —
// outer row is pure but the wrapped value is itself engaged.
// The recursion rejects.
static_assert(!detail::extract_admits_payload_v<
    Computation<Row<>, Computation<Row<Effect::Bg>, int>>>);

// Deeper nesting: pure → pure → engaged.  Recursion sees through.
static_assert(!detail::extract_admits_payload_v<
    Computation<Row<>, Computation<Row<>, Computation<Row<Effect::Bg>, int>>>>);

// Deeper nesting: pure → pure → pure.  Admits all the way down.
static_assert(detail::extract_admits_payload_v<
    Computation<Row<>, Computation<Row<>, Computation<Row<>, int>>>>);

// ── Behavioral witnesses via requires-expression ───────────────────
//
// The TYPE-LEVEL trait above is the load-bearing pin.  The POSITIVE
// requires-expression witnesses below verify that the extract
// overloads' constraints DO admit when the trait holds.
//
// The NEGATIVE direction is asserted at trait level only.  GCC 16
// treats a failed constraint inside a `!requires(...)` body as a
// hard compile error rather than substitution-failure-returns-false,
// so the `!requires(c.extract())` form does not compile (same trap
// documented for CtxOwnsAllOf in test/test_effects.cpp).  The
// trait-level `static_assert(!extract_admits_payload_v<...>)` pins
// above are the sufficient evidence the gate rejects.

// Pure outer + pure inner: extract admits.
static_assert(requires(Computation<Row<>, Computation<Row<>, int>> const& c) {
    c.extract();
}, "FIXY-FOUND-107: pure-outer + pure-inner Computation must admit extract.");

// Plain payloads still admit (regression — gate must not over-restrict).
static_assert(requires(Computation<Row<>, int> const& c) { c.extract(); },
    "FIXY-FOUND-107 regression: plain int payload must still admit extract.");

}  // namespace fixy_found_107

// ── FIXY-FOUND-108: then-callback nested-engaged payload gate ──────
//
// then(k) reuses the FIXY-FOUND-107 extract_admits_payload_v<U>
// trait on the callback's invoke_result_t::value_type.  Symmetric
// defense: 107 closed the laundering on extract's payload, 108
// closes it on then's callback-return value_type.  Together they
// prevent the Row-laundering shape Computation<Row<>, Computation
// <Row<engaged>, V>> from appearing in the chain at all.

namespace fixy_found_108 {

// Plain payload — legitimate, admits.
constexpr auto legit_callback = [](int x) {
    return Computation<Row<>, int>::mk(x + 1);
};
static_assert(requires(Computation<Row<>, int> const& c) {
    c.then(legit_callback);
}, "FIXY-FOUND-108 regression: plain-payload then-callback must admit.");

// Nested-pure payload — legitimate (R3 is empty), admits.
constexpr auto nested_pure_callback = [](int) {
    using Inner = Computation<Row<>, int>;
    return Computation<Row<>, Inner>::mk(Inner::mk(42));
};
static_assert(requires(Computation<Row<>, int> const& c) {
    c.then(nested_pure_callback);
}, "FIXY-FOUND-108 regression: nested-pure-payload then must admit "
   "(the inner row is empty — no laundering).");

// Engaged-nested payload — the laundering shape — REJECTED.
// Asserted at trait level (the !requires-around-call form hits the
// GCC 16 hard-error trap documented in fixy_found_107).
using LaunderingInner = Computation<Row<Effect::Bg>, int>;
using LaunderingCallback = decltype([](int) {
    return Computation<Row<>, LaunderingInner>::mk(
        Computation<Row<>, int>::lift<Effect::Bg>(42));
});
static_assert(
    !detail::extract_admits_payload_v<LaunderingInner>,
    "FIXY-FOUND-108: callback returning Computation<R2, Computation<"
    "Row<Bg>, U>> must NOT admit through then — inner Bg row would "
    "be laundered past the outer row_union.");

}  // namespace fixy_found_108

// ── FIXY-FOUND-014: lift<Cap> band-3 forge-status sentinel ─────────
//
// Lift's open hole (per the audit-conclusion doc-block at the
// `lift<Cap>` declaration, ~line 240) is DEMONSTRATED here as a
// structural fact rather than just narrative.  The point of this
// sentinel: a future reader sees the forge IS structurally possible
// at the type level, the `RowEngagementWitnessed<C>` band-3 concept
// in fixy/Effect.h:172 surfaces the resulting claim, and the
// rigorous closure (ctx-witnessed `lift_in<Cap>(Ctx const&, T)`)
// is the documented follow-up shape.
//
// "Forge" here is shorthand for "the type system grants the
// caller's effect claim without provenance" — symmetric with the
// weaken-from-empty forge V-219 closed (which IS structurally
// impossible now) and the then/extract laundering forges
// FIXY-FOUND-107/108 closed via `extract_admits_payload_v`.
//
// What 014 ships TODAY:
//   * (1) Source-level audit-conclusion doc-block at the lift
//         declaration making the forgeability LOUD;
//   * (2) The static_asserts below proving the forge IS possible
//         (positive demonstration of the open hole, NOT a bug —
//         the regression we'd catch is the lift becoming gate-
//         less-DIFFERENTLY, e.g. accepting non-Effect Cap values);
//   * (3) Forward-pointer to `RowEngagementProvenancePinned<C>`
//         per fixy/Effect.h:139-144 for the rigorous closure.
//
// What 014 DEFERS to a follow-up cycle (cycle-fit constraint):
//   * (a) Ctx-witnessed `lift_in<Cap, Ctx>(Ctx const&, T)` overload
//         gating on `row_contains_v<Ctx::row_type, Cap>` — requires
//         adding ExecCtx.h to Computation.h's hot-path include set
//         + updating the 10 in-file smoke-test call sites;
//   * (b) Production-side `RowEngagementProvenancePinned<C>` sibling
//         concept landing in fixy/Effect.h next to its band-3 peer;
//   * (c) The bare `lift<Cap>(x)` form gets deprecation-shim status.
//
// The CTX-WITNESSED shape is the right structural closure: a
// caller in a Bg-context (i.e., `ExecCtx<Bg, ...>{}`) CAN claim
// Bg-effect, but a caller in a fg / Init / Test context with
// `row_type == Row<>` (or `Row<X>` for X != Bg) CANNOT.  The
// substitution principle still works: lift<Cap>(x) gives Row<Cap>
// at the type level — provenance is added as a SEPARATE gate, not
// a Row<> shrinkage.

namespace fixy_found_014 {

// Demonstration that lift<Cap>(x) accepts ANY value at ANY
// IsEffect Cap — the open forge surface.  This is NOT a regression
// guard saying "the forge SHOULD be impossible"; this IS the
// audit-finding witnessed as structural fact.  The witness pins
// the SURFACE so a future contraction (e.g. requiring a cap-token
// to invoke lift) would red this fixture and force a documented
// migration.

// Plain int → Computation<Row<Bg>, int>; no Bg-effecting code
// ever ran during the production of `42`, the claim is naked.
static_assert(
    std::is_same_v<
        decltype(Computation<Row<>, int>::lift<Effect::Bg>(42)),
        Computation<Row<Effect::Bg>, int>>,
    "FIXY-FOUND-014 SURFACE: lift<Bg>(pure int) yields engaged Row<Bg>. "
    "This static_assert PASSING is the documented open-hole status. "
    "When the rigorous ctx-witnessed closure lands (lift_in<Cap,Ctx>), "
    "this sentinel is the migration anchor.");

// Same forge with IO; structural — not Bg-specific.
static_assert(
    std::is_same_v<
        decltype(Computation<Row<>, int>::lift<Effect::IO>(7)),
        Computation<Row<Effect::IO>, int>>,
    "FIXY-FOUND-014 SURFACE: lift<IO>(pure int) yields engaged Row<IO>. "
    "Same open-hole status as the Bg case above.");

// Demonstration that the GATE that IS in place — `IsEffect<Cap>` —
// correctly admits every legitimate Effect enumerator.  This pins
// the gate that EXISTS today; pairs with the static_asserts above
// showing the gate ADMITS for Bg / IO — the only Cap values lift
// is meant to accept.  The negative "lift<not-an-Effect>" case is
// covered by the IsEffect concept's own audit harness
// (test_effects.cpp) — pinning it again here would require a
// SFINAE detector that's brittle across GCC versions for static
// member templates.
static_assert(
    IsEffect<Effect::Bg>  && IsEffect<Effect::IO>
 && IsEffect<Effect::Alloc> && IsEffect<Effect::Block>,
    "FIXY-FOUND-014: lift<Cap>'s `IsEffect<Cap>` gate must accept "
    "every documented Effect enumerator.  If this reds, IsEffect "
    "stopped recognising a core Effect atom — investigate "
    "Capabilities.h:IsEffect before chasing lift call-site errors.");

// And: row arithmetic remains honest.  `effect_count_in_row()` on
// the lift result reports 1 — i.e., the lift's row claim is
// type-visible.  Companion to FOUND-051 (effect_count pinning).
static_assert(
    decltype(Computation<Row<>, int>::lift<Effect::Bg>(0))
        ::effect_count_in_row() == 1u,
    "FIXY-FOUND-014: lift<Cap> result has effect_count_in_row()==1. "
    "The forged claim IS type-visible to RowEngagementWitnessed band-3 "
    "consumers (fixy/Effect.h:172) — provenance is what's missing, "
    "not the engagement claim itself.");

// ── FIXY-FOUND-014 rigorous closure (2026-05-25 follow-up) ─────────
//
// `lift_in<Cap, Ctx>(Ctx const&, T)` is the ctx-witnessed closure.
// The gate is `row_contains_v<Ctx::row_type, Cap>` — the type-level
// proof that the caller's context permits the claimed effect.  The
// asymmetry between two canonical ExecCtx aliases (BgDrainCtx admits
// Bg; HotFgCtx does not) IS the closure.  These structural pins
// hold the asymmetry — if either reds, the rigorous closure's gate
// has lost its discriminating power and lift_in either over-admits
// (false on positive) or over-rejects (true on negative).

// Predicate the gate uses — pinned both ways:
static_assert(
    row_contains_v<typename ::crucible::effects::BgDrainCtx::row_type,
                   Effect::Bg>,
    "FIXY-FOUND-014: BgDrainCtx::row_type IS Row<Bg, Alloc> per "
    "ExecCtx.h:806-813.  BgDrainCtx MUST witness Bg — that is the "
    "production Bg-thread context, and lift_in<Bg>(BgDrainCtx{}, x) "
    "is the canonical migration target for unwitnessed lift<Bg>(x) "
    "calls in production Forge / Mimic / Cipher code.");

static_assert(
    !row_contains_v<typename ::crucible::effects::HotFgCtx::row_type,
                    Effect::Bg>,
    "FIXY-FOUND-014: HotFgCtx::row_type IS Row<> per ExecCtx.h:792-801. "
    "HotFgCtx MUST NOT witness Bg — fg-thread code in the recording "
    "pipeline must not synthesize Bg-claimed Computations.  If this "
    "reds, the closure's gate has lost its discriminating power.");

// Positive demonstration: lift_in<Bg> in BgDrainCtx admits.  The
// requires-clause's third predicate evaluates true, the function is
// in the overload set, the call succeeds, and the result type is
// the same engaged Computation the unwitnessed lift<Bg> produces.
// What's NEW is provenance: the call SITE proves the ctx-permission.
static_assert(
    std::is_same_v<
        decltype(Computation<Row<>, int>::template lift_in<Effect::Bg>(
            std::declval<::crucible::effects::BgDrainCtx const&>(), 42)),
        Computation<Row<Effect::Bg>, int>>,
    "FIXY-FOUND-014 RIGOROUS CLOSURE: lift_in<Bg>(BgDrainCtx{}, int) "
    "yields Computation<Row<Bg>, int>.  Witnessed-form ADMITS when "
    "ctx's row contains the requested effect.  Production call "
    "sites prefer this form over bare lift<Bg>(x).");

// Same effect_count pin as the bare-lift case, applied to the
// witnessed form to prove the result type is identical (only the
// CONSTRUCTION PATH gained a gate).
static_assert(
    decltype(Computation<Row<>, int>::template lift_in<Effect::Bg>(
        std::declval<::crucible::effects::BgDrainCtx const&>(), 0))
            ::effect_count_in_row() == 1u,
    "FIXY-FOUND-014 RIGOROUS CLOSURE: lift_in<Bg> result has "
    "effect_count_in_row()==1, identical to bare lift<Bg>.  The "
    "type-level engagement claim is preserved; only the construction "
    "gate gained a witness.");

}  // namespace fixy_found_014

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
