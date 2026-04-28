#pragma once

// ── crucible::safety::Consistency<Consistency_v Level, T> ───────────
//
// Type-pinned distributed-consistency wrapper.  A value of type T whose
// distributed-consistency level (EVENTUAL ⊑ READ_YOUR_WRITES ⊑
// CAUSAL_PREFIX ⊑ BOUNDED_STALENESS ⊑ STRONG) is fixed at the type
// level via the non-type template parameter Level.  Second worked
// example from the 28_04_2026_effects.md §4.2.2 catalog (FOUND-G05) —
// mechanical replication of NumericalTier proving the §4.1 wrapper-
// author template scales to a different lattice with no algebraic
// surprises.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     ConsistencyLattice::At<Level>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Level>::element_type is
//                 empty, sizeof(Consistency<Level, T>) == sizeof(T))
//
//   Use case:  25_04_2026.md §5 BatchPolicy<Axis, Level> per-parallel-
//              axis consistency declaration (Peepco OOPSLA 2025) +
//              Forge Phase K (DISTRIBUTE) per-axis consistency
//              enforcement.  A function callable signature carrying
//              Consistency<STRONG, ParameterTensor> at compile time
//              refuses to be invoked from a Consistency<EVENTUAL, ...>
//              context, fencing the "TP axis (must be STRONG)
//              accidentally configured as EVENTUAL" bug class at the
//              call site instead of in production hours later.
//
//   Axiom coverage:
//     TypeSafe — Consistency_v is a strong enum (`enum class :
//                uint8_t`); cross-level mismatches are compile errors
//                via the relax<WeakerLevel>() and satisfies<RequiredLevel>
//                gates.
//     DetSafe — every operation is constexpr; the level is a STATIC
//                property of the value, so cross-replica equivalence
//                CI can validate per-level invariants.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(Consistency<Level, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Level>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Absolute, not Comonad ─────────────────────────────
//
// A consistency-level pin is a STATIC property of the value's
// distributed-consistency promise — "this parameter shard was
// committed under STRONG consistency" — not a context the value lives
// in.  Mirrors NumericalTier (recipe-tier promise) and Linear
// (linearity grade) more than Secret (Comonad — declassify is the
// counit) or Tagged (RelativeMonad — provenance flow).  A consistency
// level describes the protocol that produced the value's bytes, not
// information ABOUT the bytes themselves.
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// Consistency subsumption-direction (per ConsistencyLattice.h L52-66):
//
//   leq(weak, strong) reads "weak is below strong in the lattice."
//   Bottom = EVENTUAL (weakest); Top = STRONG (strongest).
//
// For USE, the direction is REVERSED:
//
//   A producer at a HIGHER level (STRONG) satisfies a consumer at a
//   LOWER level (EVENTUAL).  Stronger guarantee serves weaker
//   requirement.  A Consistency<STRONG, T> can be relaxed to
//   Consistency<EVENTUAL, T> — the strongly-consistent value is
//   trivially eventually-consistent.
//
//   The converse is forbidden: a Consistency<EVENTUAL, T> CANNOT
//   become a Consistency<STRONG, T> — the eventually-consistent value
//   does NOT meet the stricter contract.  No `tighten()` method
//   exists; the only way to obtain a Consistency<STRONG, T> is to
//   construct one at the STRONG site (e.g., a Raft-committed write,
//   a CP-mode Cassandra read, or a TP-axis parameter shard commit).
//
// API:
//
//   - relax<WeakerLevel>() &  / && — convert to a less-strict level;
//                                    compile error if WeakerLevel >
//                                    Level.
//   - satisfies<RequiredLevel>     — static predicate: does this
//                                    wrapper's pinned level subsume
//                                    the required level?  Equivalent
//                                    to leq(RequiredLevel, Level).
//   - level (static constexpr)     — the pinned Consistency_v value.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has no meaningful semantics for a
// type-pinned level and is hidden by the wrapper.  The wrapper
// exposes only relax (DOWN the lattice; weaker promise still served
// by the stronger value).
//
// See ALGEBRA-15 (#460, ConsistencyLattice.h) for the underlying
// substrate; 28_04_2026_effects.md §4.2.2 for the FOUND-G05 spec
// and the production-call-site rationale; 25_04_2026.md §5 for the
// BatchPolicy<Axis, Level> per-axis consistency design.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ConsistencyLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the Consistency enum into the safety:: namespace so call
// sites don't need to spell `algebra::lattices::Consistency::STRONG`
// — matches NumericalTier's hoisting of Tolerance.
//
// NAMING NOTE: the lattice's enum is `Consistency` and the wrapper's
// class is also `Consistency<Level, T>`.  The class hides the enum
// name in `safety::` scope, so we re-export the enum under an
// unambiguous alias `Consistency_v` (the "value" form).  Production
// call sites write `Consistency<Consistency_v::STRONG, T>` for
// clarity; the alias namespace `consistency::Strong<T>` etc.
// provides shorter forms for common cases.
using ::crucible::algebra::lattices::ConsistencyLattice;
using Consistency_v = ::crucible::algebra::lattices::Consistency;

template <Consistency_v Level, typename T>
class [[nodiscard]] Consistency {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = ConsistencyLattice::At<Level>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned level — exposed as a static constexpr for callers
    // doing level-aware dispatch without instantiating the wrapper.
    static constexpr Consistency_v level = Level;

private:
    // Empty-lattice element_type collapses via [[no_unique_address]]
    // in Graded; impl_ is byte-equivalent to T at -O3.
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned level.
    //
    // SEMANTIC NOTE: a default-constructed Consistency<STRONG, T>
    // claims its T{} bytes were committed under STRONG-consistency
    // discipline.  For trivially-zero T (int{} == 0, double{} == 0.0)
    // in a freshly-initialized replica, this is vacuously true.  For
    // non-trivial T or for non-zero default values in a populated
    // replica, the claim becomes meaningful only if the wrapper is
    // constructed in a context that genuinely honors the level (e.g.
    // a Raft-committed init transaction).  Production callers SHOULD
    // prefer the explicit-T constructor at commit-completion sites
    // — the default ctor exists for compatibility with std::array<
    // Consistency<...>, N> / struct-field default-init contexts.
    constexpr Consistency() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a commit completion produces a value
    // under the declared consistency protocol; the wrapper binds
    // that level into the type.
    constexpr explicit Consistency(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction — avoids moving T through a temporary.
    // Mirrors NumericalTier's std::in_place_t pattern.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit Consistency(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — Consistency IS COPYABLE.  A
    // level pin is a static property of the value; copying a value
    // copies its consistency promise unchanged.
    constexpr Consistency(const Consistency&)            = default;
    constexpr Consistency(Consistency&&)                 = default;
    constexpr Consistency& operator=(const Consistency&) = default;
    constexpr Consistency& operator=(Consistency&&)      = default;
    ~Consistency()                                       = default;

    // Equality: compares value bytes within the SAME level pin.
    // Cross-level comparison is rejected at overload resolution
    // because the friend takes two `Consistency const&` of identical
    // <Level, T> instantiation.  Mirrors NumericalTier's family-
    // parity discipline.
    [[nodiscard]] friend constexpr bool operator==(
        Consistency const& a, Consistency const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek();
    }

    // ── Diagnostic names (forwarded from Graded substrate) ─────────
    //
    // value_type_name(): T's display string via P2996 reflection.
    // lattice_name():    "ConsistencyLattice::At<STRONG>" etc.
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

    // ── Mutable access ──────────────────────────────────────────────
    //
    // peek_mut forwards through Graded::peek_mut — admitted by the
    // Absolute modality gate.  Mutating T cannot violate the level
    // pin: the level is a TYPE-LEVEL fact about how the value was
    // committed, not about the value's current bytes.
    [[nodiscard]] constexpr T& peek_mut() & noexcept {
        return impl_.peek_mut();
    }

    // ── swap (forwarded from Graded substrate) ─────────────────────
    constexpr void swap(Consistency& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(Consistency& a, Consistency& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredLevel> — static subsumption check ────────
    //
    // True iff this wrapper's pinned level is at least as strict as
    // RequiredLevel.  Implements the lattice direction:
    //
    //   leq(RequiredLevel, Level)  reads
    //   "the required level is BELOW (or equal to) the pinned level"
    //
    // — which means the pinned level subsumes the requirement; the
    // pinned-level value is admissible at the consumer site.
    //
    // Use:
    //   static_assert(Consistency<Consistency_v::STRONG, T>
    //                     ::satisfies<Consistency_v::CAUSAL_PREFIX>);
    //   // ✓ — STRONG subsumes CAUSAL_PREFIX
    //
    //   static_assert(!Consistency<Consistency_v::EVENTUAL, T>
    //                      ::satisfies<Consistency_v::STRONG>);
    //   // ✓ — EVENTUAL does NOT subsume STRONG
    template <Consistency_v RequiredLevel>
    static constexpr bool satisfies = ConsistencyLattice::leq(RequiredLevel, Level);

    // ── relax<WeakerLevel> — convert to a less-strict level ────────
    //
    // Returns a Consistency<WeakerLevel, T> carrying the same value
    // bytes.  Allowed iff WeakerLevel ≤ Level in the lattice (the
    // weaker level is below or equal to the pinned level).  A
    // stronger promise still satisfies a weaker requirement.
    //
    // Compile error when WeakerLevel > Level — there's no way to
    // strengthen a level pin once the value was committed under a
    // weaker protocol.
    template <Consistency_v WeakerLevel>
        requires (ConsistencyLattice::leq(WeakerLevel, Level))
    [[nodiscard]] constexpr Consistency<WeakerLevel, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return Consistency<WeakerLevel, T>{this->peek()};
    }

    template <Consistency_v WeakerLevel>
        requires (ConsistencyLattice::leq(WeakerLevel, Level))
    [[nodiscard]] constexpr Consistency<WeakerLevel, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return Consistency<WeakerLevel, T>{
            std::move(impl_).consume()};
    }
};

// ── CTAD: T_at must be explicit (non-deducible NTTP) ────────────────

// ── Convenience aliases ─────────────────────────────────────────────
//
// Per-level aliases for the most common production sites.  Mirrors
// the consistency:: namespace in ConsistencyLattice.h's tier set.
namespace consistency {
    template <typename T> using Eventual         = Consistency<Consistency_v::EVENTUAL,          T>;
    template <typename T> using ReadYourWrites   = Consistency<Consistency_v::READ_YOUR_WRITES,  T>;
    template <typename T> using CausalPrefix     = Consistency<Consistency_v::CAUSAL_PREFIX,     T>;
    template <typename T> using BoundedStaleness = Consistency<Consistency_v::BOUNDED_STALENESS, T>;
    template <typename T> using Strong           = Consistency<Consistency_v::STRONG,            T>;
}  // namespace consistency

// ── Layout invariants ───────────────────────────────────────────────
//
// regime-1: zero-cost EBO collapse.  sizeof(Consistency<Level, T>)
// == sizeof(T) at every supported level.  Witnessed at three T sizes
// (1B, 4B, 8B) and across the full level spectrum.
namespace detail::consistency_layout {

template <typename T> using StrongC   = Consistency<Consistency_v::STRONG,            T>;
template <typename T> using BoundedC  = Consistency<Consistency_v::BOUNDED_STALENESS, T>;
template <typename T> using EventualC = Consistency<Consistency_v::EVENTUAL,          T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongC,   char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongC,   int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(StrongC,   double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedC,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(BoundedC,  double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(EventualC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(EventualC, double);

}  // namespace detail::consistency_layout

// Direct sizeof witnesses — EBO collapse must hold for the
// production-typical T sizes regardless of which level is pinned.
static_assert(sizeof(Consistency<Consistency_v::EVENTUAL,          int>)    == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::READ_YOUR_WRITES,  int>)    == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::CAUSAL_PREFIX,     int>)    == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::BOUNDED_STALENESS, int>)    == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::STRONG,            int>)    == sizeof(int));
static_assert(sizeof(Consistency<Consistency_v::STRONG,            double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::consistency_self_test {

using StrongInt   = Consistency<Consistency_v::STRONG,           int>;
using CausalInt   = Consistency<Consistency_v::CAUSAL_PREFIX,    int>;
using EventualInt = Consistency<Consistency_v::EVENTUAL,         int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr StrongInt c_default{};
static_assert(c_default.peek() == 0);
static_assert(c_default.level == Consistency_v::STRONG);

inline constexpr StrongInt c_explicit{42};
static_assert(c_explicit.peek() == 42);

// ── Pinned level accessor ──────────────────────────────────────────
static_assert(StrongInt::level   == Consistency_v::STRONG);
static_assert(CausalInt::level   == Consistency_v::CAUSAL_PREFIX);
static_assert(EventualInt::level == Consistency_v::EVENTUAL);

// ── satisfies<RequiredLevel> — subsumption-up direction ────────────
//
// A STRONG producer satisfies every consumer.
static_assert(StrongInt::satisfies<Consistency_v::STRONG>);
static_assert(StrongInt::satisfies<Consistency_v::BOUNDED_STALENESS>);
static_assert(StrongInt::satisfies<Consistency_v::CAUSAL_PREFIX>);
static_assert(StrongInt::satisfies<Consistency_v::READ_YOUR_WRITES>);
static_assert(StrongInt::satisfies<Consistency_v::EVENTUAL>);

// A CAUSAL_PREFIX producer satisfies weaker-or-equal consumers only.
static_assert( CausalInt::satisfies<Consistency_v::CAUSAL_PREFIX>);    // self
static_assert( CausalInt::satisfies<Consistency_v::READ_YOUR_WRITES>); // weaker
static_assert( CausalInt::satisfies<Consistency_v::EVENTUAL>);
static_assert(!CausalInt::satisfies<Consistency_v::BOUNDED_STALENESS>); // stronger
static_assert(!CausalInt::satisfies<Consistency_v::STRONG>);

// An EVENTUAL producer satisfies only EVENTUAL consumers.
static_assert( EventualInt::satisfies<Consistency_v::EVENTUAL>);
static_assert(!EventualInt::satisfies<Consistency_v::READ_YOUR_WRITES>);
static_assert(!EventualInt::satisfies<Consistency_v::STRONG>);

// ── relax<WeakerLevel> — DOWN-the-lattice conversion ───────────────
//
// STRONG relaxes to any level.
inline constexpr auto from_strong_to_causal =
    StrongInt{42}.relax<Consistency_v::CAUSAL_PREFIX>();
static_assert(from_strong_to_causal.peek() == 42);
static_assert(from_strong_to_causal.level == Consistency_v::CAUSAL_PREFIX);

inline constexpr auto from_strong_to_eventual =
    StrongInt{99}.relax<Consistency_v::EVENTUAL>();
static_assert(from_strong_to_eventual.peek() == 99);
static_assert(from_strong_to_eventual.level == Consistency_v::EVENTUAL);

// CAUSAL_PREFIX relaxes to weaker (RYW / EVENTUAL) but NOT to
// stronger (BOUNDED_STALENESS / STRONG).
inline constexpr auto from_causal_to_ryw =
    CausalInt{7}.relax<Consistency_v::READ_YOUR_WRITES>();
static_assert(from_causal_to_ryw.peek() == 7);
static_assert(from_causal_to_ryw.level == Consistency_v::READ_YOUR_WRITES);

inline constexpr auto from_causal_to_self =
    CausalInt{8}.relax<Consistency_v::CAUSAL_PREFIX>();   // identity relax
static_assert(from_causal_to_self.peek() == 8);

// SFINAE-style detector pinning the requires-clause.
template <typename W, Consistency_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<StrongInt,   Consistency_v::CAUSAL_PREFIX>);     // ✓ down
static_assert( can_relax<StrongInt,   Consistency_v::EVENTUAL>);          // ✓ down
static_assert( can_relax<CausalInt,   Consistency_v::READ_YOUR_WRITES>);  // ✓ down
static_assert( can_relax<CausalInt,   Consistency_v::CAUSAL_PREFIX>);     // ✓ self
static_assert(!can_relax<CausalInt,   Consistency_v::BOUNDED_STALENESS>); // ✗ up
static_assert(!can_relax<CausalInt,   Consistency_v::STRONG>);            // ✗ up
static_assert(!can_relax<EventualInt, Consistency_v::READ_YOUR_WRITES>);  // ✗ up

// ── Diagnostic forwarders ─────────────────────────────────────────
//
// Per the gcc16_c26_reflection_gotchas memory rule: use ends_with
// rather than == for value_type_name (display_string_of is TU-
// context-fragile).
static_assert(StrongInt::value_type_name().ends_with("int"));

// lattice_name forwards to the hand-written At<Level>::name string.
static_assert(StrongInt::lattice_name()   == "ConsistencyLattice::At<STRONG>");
static_assert(CausalInt::lattice_name()   == "ConsistencyLattice::At<CAUSAL_PREFIX>");
static_assert(EventualInt::lattice_name() == "ConsistencyLattice::At<EVENTUAL>");

// ── swap exchanges T values within the same level pin ─────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_level() noexcept {
    StrongInt a{10};
    StrongInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_level());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    StrongInt a{10};
    StrongInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    StrongInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── operator== — same-level, same-T comparison ────────────────────
[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    StrongInt a{42};
    StrongInt b{42};
    StrongInt c{43};
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

static_assert( can_equality_compare<StrongInt>);
static_assert(!can_equality_compare<Consistency<Consistency_v::STRONG, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    StrongInt a{99};
    auto b = a.relax<Consistency_v::STRONG>();
    return b.peek() == 99 && b.level == Consistency_v::STRONG;
}
static_assert(relax_to_self_is_identity());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(StrongInt::value_type_name().size() > 0);
static_assert(StrongInt::lattice_name().size() > 0);
static_assert(StrongInt::lattice_name().starts_with("ConsistencyLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(consistency::Strong<int>::level         == Consistency_v::STRONG);
static_assert(consistency::BoundedStaleness<int>::level == Consistency_v::BOUNDED_STALENESS);
static_assert(consistency::CausalPrefix<int>::level   == Consistency_v::CAUSAL_PREFIX);
static_assert(consistency::ReadYourWrites<int>::level == Consistency_v::READ_YOUR_WRITES);
static_assert(consistency::Eventual<int>::level       == Consistency_v::EVENTUAL);

static_assert(std::is_same_v<consistency::Strong<double>,
                             Consistency<Consistency_v::STRONG, double>>);

// ── Runtime smoke test ─────────────────────────────────────────────
//
// Per feedback_algebra_runtime_smoke_test_discipline memory: exercise
// every named operation with non-constant arguments at runtime.
inline void runtime_smoke_test() {
    // Construction paths — default + explicit + in_place.
    StrongInt a{};
    StrongInt b{42};
    StrongInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // Static level accessor — verified at runtime.
    if (StrongInt::level != Consistency_v::STRONG) {
        std::abort();
    }

    // peek_mut — in-place mutation at runtime.
    StrongInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    // Swap — at runtime to exercise non-constexpr exchange.
    StrongInt sx{1};
    StrongInt sy{2};
    sx.swap(sy);

    // Free-function swap (ADL).
    using std::swap;
    swap(sx, sy);

    // relax<WeakerLevel> — both const& and && overloads.
    StrongInt source{77};
    auto relaxed_copy = source.relax<Consistency_v::CAUSAL_PREFIX>();
    auto relaxed_move = std::move(source).relax<Consistency_v::EVENTUAL>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    // satisfies<...> — runtime-readable static_constexpr predicate.
    [[maybe_unused]] bool s1 = StrongInt::satisfies<Consistency_v::CAUSAL_PREFIX>;
    [[maybe_unused]] bool s2 = CausalInt::satisfies<Consistency_v::STRONG>;

    // operator== — same-level comparison at runtime.
    StrongInt eq_a{42};
    StrongInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct a new instance from consumed inner.
    StrongInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation at runtime.
    consistency::Strong<int>          alias_form{123};
    consistency::CausalPrefix<double> causal_form{3.14};
    [[maybe_unused]] auto av = alias_form.peek();
    [[maybe_unused]] auto cv = causal_form.peek();
}

}  // namespace detail::consistency_self_test

}  // namespace crucible::safety
