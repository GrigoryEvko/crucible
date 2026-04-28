#pragma once

// ── crucible::safety::OpaqueLifetime<Lifetime_v Scope, T> ───────────
//
// Type-pinned distributed-state-lifetime wrapper.  A value of type T
// whose lifetime scope (PER_REQUEST ⊑ PER_PROGRAM ⊑ PER_FLEET) is
// fixed at the type level via the non-type template parameter Scope.
// Third worked example from the 28_04_2026_effects.md §4.2.3 catalog
// (FOUND-G09) — completes the Month-1 ready-to-ship batch.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     LifetimeLattice::At<Scope>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Scope>::element_type is
//                 empty, sizeof(OpaqueLifetime<Scope, T>) == sizeof(T))
//
//   Use case:  25_04_2026.md §16 SessionOpaqueState + SchedulerDirective
//              inferlet pattern (Pie SOSP 2025) + Cipher tier promotion.
//              An inferlet user state declared OpaqueLifetime<PER_REQUEST,
//              PdaState> for grammar-constrained decoding cannot
//              accidentally be persisted to PER_FLEET cold storage,
//              fencing the "request-scoped state silently committed
//              across requests" cross-request data leak at the
//              persistence boundary instead of in production hours
//              later.
//
//   Axiom coverage:
//     TypeSafe — Lifetime_v is a strong enum (`enum class : uint8_t`);
//                cross-scope mismatches are compile errors via the
//                relax<NarrowerScope>() and satisfies<RequiredScope>
//                gates.
//     DetSafe — every operation is constexpr; the scope is a STATIC
//                property of the value, so cross-replica equivalence
//                CI can validate per-scope persistence invariants.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(OpaqueLifetime<Scope, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Scope>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A lifetime-scope pin is a STATIC property of the value's
// persistence promise — "this state was declared PER_REQUEST" —
// not a context the value lives in.  Mirrors NumericalTier and
// Consistency more than Secret (Comonad declassify) or Tagged
// (RelativeMonad provenance).
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// Lifetime subsumption-direction (per LifetimeLattice.h L37-43):
//
//   leq(narrow, wide) reads "narrow's lifetime fits inside wide's."
//   Bottom = PER_REQUEST (narrowest); Top = PER_FLEET (widest).
//
// For USE, the direction is REVERSED:
//
//   A producer at a WIDER scope (PER_FLEET) satisfies a consumer at
//   a NARROWER scope (PER_REQUEST).  Wider availability serves
//   narrower requirement.  A OpaqueLifetime<PER_FLEET, T> can be
//   relaxed to OpaqueLifetime<PER_REQUEST, T> — the fleet-scoped
//   value is trivially available within a single request.
//
//   The converse is forbidden: an OpaqueLifetime<PER_REQUEST, T>
//   CANNOT become an OpaqueLifetime<PER_FLEET, T> — the request-
//   scoped value dies when the request ends; widening its scope
//   to PER_FLEET would leak data across requests.  No `widen()`
//   method exists; the only way to obtain an OpaqueLifetime<
//   PER_FLEET, T> is to construct one at a fleet-scoped commit
//   site (e.g., a Raft-committed cold-tier write).
//
// API:
//
//   - relax<NarrowerScope>() &  / && — convert to a narrower scope;
//                                      compile error if NarrowerScope
//                                      > Scope.
//   - satisfies<RequiredScope>       — static predicate: does this
//                                      wrapper's pinned scope subsume
//                                      the required scope?  Equivalent
//                                      to leq(RequiredScope, Scope).
//   - scope (static constexpr)       — the pinned Lifetime_v value.
//
// SEMANTIC NOTE on the "relax" naming: for Lifetime, "narrowing the
// scope" might intuitively read as "tightening" (smaller availability
// window).  But operationally it RELAXES the persistence guarantee —
// you stop promising the value will be available beyond the narrower
// scope.  The API uses `relax` for uniformity with NumericalTier and
// Consistency; the docblock here documents the semantic mapping.
//
// `Graded::weaken` on the substrate goes UP the lattice (wider scope)
// — that operation has no meaningful semantics for a type-pinned
// scope and would be the cross-request leak path.  Hidden by the
// wrapper.
//
// See ALGEBRA-16 (#461, LifetimeLattice.h) for the underlying
// substrate; 28_04_2026_effects.md §4.2.3 for the FOUND-G09 spec
// and the production-call-site rationale; 25_04_2026.md §16 for
// the SessionOpaqueState design.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/LifetimeLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the Lifetime enum into the safety:: namespace under an
// unambiguous alias.  No name collision concern here (the wrapper
// is OpaqueLifetime, not Lifetime), but using `Lifetime_v` matches
// Consistency's `Consistency_v` convention for cross-wrapper
// uniformity.
using ::crucible::algebra::lattices::LifetimeLattice;
using Lifetime_v = ::crucible::algebra::lattices::Lifetime;

template <Lifetime_v Scope, typename T>
class [[nodiscard]] OpaqueLifetime {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = LifetimeLattice::At<Scope>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned scope — exposed as a static constexpr for callers
    // doing scope-aware dispatch without instantiating the wrapper.
    static constexpr Lifetime_v scope = Scope;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned scope.
    //
    // SEMANTIC NOTE: a default-constructed OpaqueLifetime<PER_FLEET, T>
    // claims its T{} bytes were committed under PER_FLEET-scope
    // discipline.  For trivially-zero T in a freshly-initialized
    // fleet-state slot, this is vacuously true.  For non-trivial T
    // or non-zero T{} in a populated fleet, the claim becomes
    // meaningful only if the wrapper is constructed in a context
    // that genuinely honors the scope (e.g., a Raft-committed cold-
    // tier init).  Production callers SHOULD prefer the explicit-T
    // constructor at scope-anchored commit sites.
    constexpr OpaqueLifetime() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.
    constexpr explicit OpaqueLifetime(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit OpaqueLifetime(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — OpaqueLifetime IS COPYABLE
    // within the same scope pin.  Copying a value within its
    // declared scope is fine; the per-scope identity is preserved
    // by template-instantiation isolation.
    constexpr OpaqueLifetime(const OpaqueLifetime&)            = default;
    constexpr OpaqueLifetime(OpaqueLifetime&&)                 = default;
    constexpr OpaqueLifetime& operator=(const OpaqueLifetime&) = default;
    constexpr OpaqueLifetime& operator=(OpaqueLifetime&&)      = default;
    ~OpaqueLifetime()                                          = default;

    // Equality: compares value bytes within the SAME scope pin.
    // Cross-scope comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        OpaqueLifetime const& a, OpaqueLifetime const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    [[nodiscard]] static consteval std::string_view value_type_name() noexcept {
        return graded_type::value_type_name();
    }
    [[nodiscard]] static consteval std::string_view lattice_name() noexcept {
        return graded_type::lattice_name();
    }

    // ── Read-only access ────────────────────────────────────────────
    [[nodiscard]] constexpr T const& peek() const& noexcept {
        return impl_.peek();
    }

    [[nodiscard]] constexpr T consume() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return std::move(impl_).consume();
    }

    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(OpaqueLifetime& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(OpaqueLifetime& a, OpaqueLifetime& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredScope> — static subsumption check ────────
    //
    // True iff this wrapper's pinned scope is at least as wide as
    // RequiredScope.  Wider scope satisfies narrower requirement
    // (a fleet-scoped value is available within any single request).
    template <Lifetime_v RequiredScope>
    static constexpr bool satisfies = LifetimeLattice::leq(RequiredScope, Scope);

    // ── relax<NarrowerScope> — convert to a narrower scope ─────────
    //
    // Returns an OpaqueLifetime<NarrowerScope, T> carrying the same
    // value bytes.  Allowed iff NarrowerScope ≤ Scope in the
    // lattice (the narrower scope is below or equal to the pinned
    // scope).  Wider availability still serves narrower requirement.
    //
    // Compile error when NarrowerScope > Scope — would widen the
    // scope and leak data across the original scope boundary.
    template <Lifetime_v NarrowerScope>
        requires (LifetimeLattice::leq(NarrowerScope, Scope))
    [[nodiscard]] constexpr OpaqueLifetime<NarrowerScope, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return OpaqueLifetime<NarrowerScope, T>{this->peek()};
    }

    template <Lifetime_v NarrowerScope>
        requires (LifetimeLattice::leq(NarrowerScope, Scope))
    [[nodiscard]] constexpr OpaqueLifetime<NarrowerScope, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return OpaqueLifetime<NarrowerScope, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace opaque_lifetime {
    template <typename T> using PerRequest = OpaqueLifetime<Lifetime_v::PER_REQUEST, T>;
    template <typename T> using PerProgram = OpaqueLifetime<Lifetime_v::PER_PROGRAM, T>;
    template <typename T> using PerFleet   = OpaqueLifetime<Lifetime_v::PER_FLEET,   T>;
}  // namespace opaque_lifetime

// ── Layout invariants ───────────────────────────────────────────────
//
// regime-1: zero-cost EBO collapse.
namespace detail::opaque_lifetime_layout {

template <typename T> using FleetL   = OpaqueLifetime<Lifetime_v::PER_FLEET,   T>;
template <typename T> using ProgramL = OpaqueLifetime<Lifetime_v::PER_PROGRAM, T>;
template <typename T> using RequestL = OpaqueLifetime<Lifetime_v::PER_REQUEST, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetL,   char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetL,   int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(FleetL,   double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProgramL, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ProgramL, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RequestL, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(RequestL, double);

}  // namespace detail::opaque_lifetime_layout

static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_REQUEST, int>)    == sizeof(int));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>)    == sizeof(int));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET,   int>)    == sizeof(int));
static_assert(sizeof(OpaqueLifetime<Lifetime_v::PER_FLEET,   double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::opaque_lifetime_self_test {

using FleetInt   = OpaqueLifetime<Lifetime_v::PER_FLEET,   int>;
using ProgramInt = OpaqueLifetime<Lifetime_v::PER_PROGRAM, int>;
using RequestInt = OpaqueLifetime<Lifetime_v::PER_REQUEST, int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr FleetInt o_default{};
static_assert(o_default.peek() == 0);
static_assert(o_default.scope == Lifetime_v::PER_FLEET);

inline constexpr FleetInt o_explicit{42};
static_assert(o_explicit.peek() == 42);

// ── Pinned scope accessor ──────────────────────────────────────────
static_assert(FleetInt::scope   == Lifetime_v::PER_FLEET);
static_assert(ProgramInt::scope == Lifetime_v::PER_PROGRAM);
static_assert(RequestInt::scope == Lifetime_v::PER_REQUEST);

// ── satisfies<RequiredScope> — wider satisfies narrower ────────────
//
// PER_FLEET satisfies every consumer.
static_assert(FleetInt::satisfies<Lifetime_v::PER_FLEET>);
static_assert(FleetInt::satisfies<Lifetime_v::PER_PROGRAM>);
static_assert(FleetInt::satisfies<Lifetime_v::PER_REQUEST>);

// PER_PROGRAM satisfies narrower-or-equal.
static_assert( ProgramInt::satisfies<Lifetime_v::PER_PROGRAM>);
static_assert( ProgramInt::satisfies<Lifetime_v::PER_REQUEST>);
static_assert(!ProgramInt::satisfies<Lifetime_v::PER_FLEET>);

// PER_REQUEST satisfies only PER_REQUEST.
static_assert( RequestInt::satisfies<Lifetime_v::PER_REQUEST>);
static_assert(!RequestInt::satisfies<Lifetime_v::PER_PROGRAM>);
static_assert(!RequestInt::satisfies<Lifetime_v::PER_FLEET>);

// ── relax<NarrowerScope> — DOWN-the-lattice conversion ────────────
//
// PER_FLEET relaxes to any scope.
inline constexpr auto from_fleet_to_program =
    FleetInt{42}.relax<Lifetime_v::PER_PROGRAM>();
static_assert(from_fleet_to_program.peek() == 42);
static_assert(from_fleet_to_program.scope == Lifetime_v::PER_PROGRAM);

inline constexpr auto from_fleet_to_request =
    FleetInt{99}.relax<Lifetime_v::PER_REQUEST>();
static_assert(from_fleet_to_request.peek() == 99);
static_assert(from_fleet_to_request.scope == Lifetime_v::PER_REQUEST);

// PER_PROGRAM relaxes to PER_REQUEST but NOT to PER_FLEET.
inline constexpr auto from_program_to_request =
    ProgramInt{7}.relax<Lifetime_v::PER_REQUEST>();
static_assert(from_program_to_request.peek() == 7);
static_assert(from_program_to_request.scope == Lifetime_v::PER_REQUEST);

inline constexpr auto from_program_to_self =
    ProgramInt{8}.relax<Lifetime_v::PER_PROGRAM>();   // identity
static_assert(from_program_to_self.peek() == 8);

// SFINAE-style detector pinning the requires-clause.
template <typename W, Lifetime_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<FleetInt,   Lifetime_v::PER_PROGRAM>); // ✓ down
static_assert( can_relax<FleetInt,   Lifetime_v::PER_REQUEST>); // ✓ down
static_assert( can_relax<ProgramInt, Lifetime_v::PER_REQUEST>); // ✓ down
static_assert( can_relax<ProgramInt, Lifetime_v::PER_PROGRAM>); // ✓ self
static_assert(!can_relax<ProgramInt, Lifetime_v::PER_FLEET>);   // ✗ up
static_assert(!can_relax<RequestInt, Lifetime_v::PER_PROGRAM>); // ✗ up
static_assert(!can_relax<RequestInt, Lifetime_v::PER_FLEET>);   // ✗ up

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(FleetInt::value_type_name().ends_with("int"));
static_assert(FleetInt::lattice_name()   == "LifetimeLattice::At<PER_FLEET>");
static_assert(ProgramInt::lattice_name() == "LifetimeLattice::At<PER_PROGRAM>");
static_assert(RequestInt::lattice_name() == "LifetimeLattice::At<PER_REQUEST>");

// ── swap exchanges T values within the same scope pin ─────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_scope() noexcept {
    FleetInt a{10};
    FleetInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_scope());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    FleetInt a{10};
    FleetInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    FleetInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── operator== — same-scope, same-T comparison ────────────────────
[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    FleetInt a{42};
    FleetInt b{42};
    FleetInt c{43};
    return (a == b) && !(a == c);
}
static_assert(equality_compares_value_bytes());

// SFINAE: operator== is only present when T has its own ==.
struct NoEqualityT {
    int v{0};
    NoEqualityT() = default;
    explicit NoEqualityT(int x) : v{x} {}
    NoEqualityT(NoEqualityT&&) = default;
    NoEqualityT& operator=(NoEqualityT&&) = default;
    NoEqualityT(NoEqualityT const&) = delete;
    NoEqualityT& operator=(NoEqualityT const&) = delete;
};

template <typename W>
concept can_equality_compare = requires(W const& a, W const& b) {
    { a == b } -> std::convertible_to<bool>;
};

static_assert( can_equality_compare<FleetInt>);
static_assert(!can_equality_compare<OpaqueLifetime<Lifetime_v::PER_FLEET, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    FleetInt a{99};
    auto b = a.relax<Lifetime_v::PER_FLEET>();
    return b.peek() == 99 && b.scope == Lifetime_v::PER_FLEET;
}
static_assert(relax_to_self_is_identity());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(FleetInt::value_type_name().size() > 0);
static_assert(FleetInt::lattice_name().size() > 0);
static_assert(FleetInt::lattice_name().starts_with("LifetimeLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(opaque_lifetime::PerFleet<int>::scope   == Lifetime_v::PER_FLEET);
static_assert(opaque_lifetime::PerProgram<int>::scope == Lifetime_v::PER_PROGRAM);
static_assert(opaque_lifetime::PerRequest<int>::scope == Lifetime_v::PER_REQUEST);

static_assert(std::is_same_v<opaque_lifetime::PerFleet<double>,
                             OpaqueLifetime<Lifetime_v::PER_FLEET, double>>);

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Construction paths.
    FleetInt a{};
    FleetInt b{42};
    FleetInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // Static scope accessor at runtime.
    if (FleetInt::scope != Lifetime_v::PER_FLEET) {
        std::abort();
    }

    // peek_mut.
    FleetInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    // Swap at runtime.
    FleetInt sx{1};
    FleetInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // relax<NarrowerScope> — both overloads.
    FleetInt source{77};
    auto narrowed_copy = source.relax<Lifetime_v::PER_PROGRAM>();
    auto narrowed_move = std::move(source).relax<Lifetime_v::PER_REQUEST>();
    [[maybe_unused]] auto rcopy = narrowed_copy.peek();
    [[maybe_unused]] auto rmove = narrowed_move.peek();

    // satisfies<...> at runtime.
    [[maybe_unused]] bool s1 = FleetInt::satisfies<Lifetime_v::PER_REQUEST>;
    [[maybe_unused]] bool s2 = ProgramInt::satisfies<Lifetime_v::PER_FLEET>;

    // operator== — same-scope.
    FleetInt eq_a{42};
    FleetInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct from consumed inner.
    FleetInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation.
    opaque_lifetime::PerFleet<int>      alias_form{123};
    opaque_lifetime::PerRequest<double> req_form{3.14};
    [[maybe_unused]] auto av = alias_form.peek();
    [[maybe_unused]] auto rv = req_form.peek();
}

}  // namespace detail::opaque_lifetime_self_test

}  // namespace crucible::safety
