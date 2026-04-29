#pragma once

// ── crucible::safety::HotPath<HotPathTier_v Tier, T> ────────────────
//
// Type-pinned hot-path-budget wrapper.  A value of type T whose
// hot-path budget tier (Cold ⊑ Warm ⊑ Hot) is fixed at the type
// level via the non-type template parameter Tier.  Second Month-2
// wrapper from the 28_04_2026_effects.md §4.3.2 catalog (FOUND-G19)
// — composes directly with DetSafe in the canonical wrapper-nesting
// order:
//
//   HotPath<Hot,
//       DetSafe<Pure,
//           NumericalTier<BITEXACT, T>>>
//
// Each layer EBO-collapses; the wrapper-nesting cost is sizeof(T)
// at -O3.  Per 28_04 §4.7: wrappers compose orthogonally; the
// dispatcher (FOUND-D) reads the stack via reflection.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     HotPathLattice::At<Tier>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//                 empty, sizeof(HotPath<Tier, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.2):
//     - TraceRing::try_push  — declared `Hot` (no alloc / syscall / block)
//     - MetaLog::push        — declared `Hot`
//     - Vessel::dispatch_op  — declared `Hot` (foreground path)
//     - BackgroundThread::drain     — declared `Warm` (alloc OK)
//     - KernelCache compile worker  — declared `Warm`
//     - Cipher::flush_cold   — declared `Cold` (block + IO OK)
//     - Canopy gossip writer — declared `Cold`
//
//   The bug class caught: a refactor that adds `printf` (or any
//   block / syscall) to a hot-path function.  Today caught by
//   review or perf regression hours later; with the wrapper, becomes
//   a compile error at the call boundary because the function's
//   row would have to weaken from `Hot` to `Cold` — and the caller's
//   declared `requires Hot` rejects it.
//
//   Axiom coverage:
//     TypeSafe — HotPathTier_v is a strong scoped enum;
//                cross-tier mismatches are compile errors via the
//                relax<WeakerTier>() and satisfies<RequiredTier>
//                gates.
//     DetSafe — orthogonal axis; HotPath does NOT itself enforce
//                determinism.  Composes via wrapper-nesting.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(HotPath<Tier, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Tier>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A hot-path budget pin is a STATIC property of WHAT the function
// promises to do (operation classes admitted / forbidden) — not a
// context the value lives in.  The bytes themselves carry no
// information about the budget; the wrapper carries that information
// at the TYPE level.  Mirrors NumericalTier (recipe-tier promise),
// Consistency (commit protocol), OpaqueLifetime (declared scope),
// DetSafe (determinism-safety pin) — all Absolute modalities over
// At<>-pinned grades.
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// HotPath subsumption-direction (per HotPathLattice.h docblock):
//
//   leq(weaker, stronger) reads "weaker-budget is below stronger-
//   budget in the lattice."
//   Bottom = Cold (weakest); Top = Hot (strongest).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER tier (Hot) satisfies a consumer at a
//   WEAKER tier (Warm).  Stronger budget serves weaker requirement.
//   A HotPath<Hot, T> can be relaxed to HotPath<Warm, T> — the
//   Hot-budget value trivially satisfies the Warm-acceptance gate.
//
//   The converse is forbidden: a HotPath<Cold, T> CANNOT become a
//   HotPath<Hot, T> — the cold-tier value performed work that the
//   hot path forbids; relaxing the type to claim Hot compliance
//   would defeat the hot-path discipline.  No `tighten()` method
//   exists; the only way to obtain a HotPath<Hot, T> is to
//   construct one at a genuinely-hot-path-safe call site (e.g.,
//   TraceRing::try_push, Vessel::dispatch_op).
//
// API:
//
//   - relax<WeakerTier>() &  / && — convert to a less-strict tier;
//                                   compile error if WeakerTier >
//                                   Tier (would CLAIM more hot-path
//                                   compliance than the source
//                                   provides).
//   - satisfies<RequiredTier>     — static predicate: does this
//                                   wrapper's pinned tier subsume
//                                   the required tier?  Equivalent
//                                   to leq(RequiredTier, Tier).
//   - tier (static constexpr)     — the pinned HotPathTier_v value.
//
// SEMANTIC NOTE on the "relax" naming: for HotPath, "weakening the
// tier" means accepting MORE permissive budgets (going down the
// chain Cold ← Warm ← Hot).  Calling `relax<Cold>()` on a Hot-pinned
// value means "I'm OK treating this Hot value as Cold-tolerable
// here."  This is a downgrade of the BUDGET guarantee, not the
// value's inherent purity.  The API uses `relax` for uniformity
// with NumericalTier / Consistency / OpaqueLifetime / DetSafe.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has NO MEANINGFUL SEMANTICS for a
// type-pinned tier and would be the LOAD-BEARING BUG: a Cold-tier
// value claiming Hot compliance would defeat the hot-path
// discipline.  Hidden by the wrapper.
//
// See FOUND-G18 (algebra/lattices/HotPathLattice.h) for the
// underlying substrate; 28_04_2026_effects.md §4.3.2 + §4.7 for
// the production-call-site rationale and the canonical wrapper-
// nesting story; CLAUDE.md §IX (concurrency / cache-tier discipline)
// for the hot-path semantics.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/HotPathLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the HotPathTier enum into the safety:: namespace under
// `HotPathTier_v`.  No name collision — the wrapper class is
// `HotPath`, not `HotPathTier`.  Naming convention matches
// DetSafeTier_v + Consistency_v + Lifetime_v from sibling wrappers.
using ::crucible::algebra::lattices::HotPathLattice;
using HotPathTier_v = ::crucible::algebra::lattices::HotPathTier;

template <HotPathTier_v Tier, typename T>
class [[nodiscard]] HotPath {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = HotPathLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned tier — exposed as a static constexpr for callers
    // doing tier-aware dispatch without instantiating the wrapper.
    static constexpr HotPathTier_v tier = Tier;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned tier.
    //
    // SEMANTIC NOTE: a default-constructed HotPath<Hot, T> claims
    // its T{} bytes were produced under Hot discipline.  For
    // trivially-zero T, this is vacuously true.  For non-trivial T
    // or non-zero T{} in a populated context, the claim becomes
    // meaningful only if the wrapper is constructed in a context
    // that genuinely honors the tier (e.g., a TraceRing entry built
    // entirely from stack-allocated metadata, no allocator
    // invocations).  Production callers SHOULD prefer the explicit-T
    // constructor at tier-anchored production sites; the default
    // ctor exists for compatibility with std::array<HotPath<Hot, T>,
    // N> / struct-field default-init contexts.
    constexpr HotPath() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a hot-path-safe production site
    // constructs the wrapper at the appropriate tier.
    constexpr explicit HotPath(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit HotPath(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — HotPath IS COPYABLE within the
    // same tier pin.
    constexpr HotPath(const HotPath&)            = default;
    constexpr HotPath(HotPath&&)                 = default;
    constexpr HotPath& operator=(const HotPath&) = default;
    constexpr HotPath& operator=(HotPath&&)      = default;
    ~HotPath()                                   = default;

    // Equality: compares value bytes within the SAME tier pin.
    // Cross-tier comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        HotPath const& a, HotPath const& b) noexcept(
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
    constexpr void swap(HotPath& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(HotPath& a, HotPath& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredTier> — static subsumption check ────────
    //
    // True iff this wrapper's pinned tier is at least as strong as
    // RequiredTier.  Stronger budget satisfies weaker requirement
    // (a Hot-pinned value is admissible at a Warm-accepting
    // consumer).
    //
    // Use:
    //   static_assert(HotPath<HotPathTier_v::Hot, T>
    //                     ::satisfies<HotPathTier_v::Warm>);
    //   // ✓ — Hot subsumes Warm
    //
    //   static_assert(!HotPath<HotPathTier_v::Cold, T>
    //                      ::satisfies<HotPathTier_v::Hot>);
    //   // ✓ — Cold does NOT subsume Hot
    template <HotPathTier_v RequiredTier>
    static constexpr bool satisfies = HotPathLattice::leq(RequiredTier, Tier);

    // ── relax<WeakerTier> — convert to a less-strict tier ─────────
    //
    // Returns a HotPath<WeakerTier, T> carrying the same value
    // bytes.  Allowed iff WeakerTier ≤ Tier in the lattice (the
    // weaker tier is below or equal to the pinned tier).  Stronger
    // budget still serves weaker requirement.
    //
    // Compile error when WeakerTier > Tier — would CLAIM more
    // hot-path compliance than the source provides.
    template <HotPathTier_v WeakerTier>
        requires (HotPathLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr HotPath<WeakerTier, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return HotPath<WeakerTier, T>{this->peek()};
    }

    template <HotPathTier_v WeakerTier>
        requires (HotPathLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr HotPath<WeakerTier, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return HotPath<WeakerTier, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace hot_path {
    template <typename T> using Hot  = HotPath<HotPathTier_v::Hot,  T>;
    template <typename T> using Warm = HotPath<HotPathTier_v::Warm, T>;
    template <typename T> using Cold = HotPath<HotPathTier_v::Cold, T>;
}  // namespace hot_path

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::hot_path_layout {

template <typename T> using HotH  = HotPath<HotPathTier_v::Hot,  T>;
template <typename T> using WarmH = HotPath<HotPathTier_v::Warm, T>;
template <typename T> using ColdH = HotPath<HotPathTier_v::Cold, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotH,  char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotH,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotH,  double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmH, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmH, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdH, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdH, double);

}  // namespace detail::hot_path_layout

static_assert(sizeof(HotPath<HotPathTier_v::Hot,  int>)    == sizeof(int));
static_assert(sizeof(HotPath<HotPathTier_v::Warm, int>)    == sizeof(int));
static_assert(sizeof(HotPath<HotPathTier_v::Cold, int>)    == sizeof(int));
static_assert(sizeof(HotPath<HotPathTier_v::Hot,  double>) == sizeof(double));
static_assert(sizeof(HotPath<HotPathTier_v::Warm, double>) == sizeof(double));
static_assert(sizeof(HotPath<HotPathTier_v::Cold, double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::hot_path_self_test {

using HotInt  = HotPath<HotPathTier_v::Hot,  int>;
using WarmInt = HotPath<HotPathTier_v::Warm, int>;
using ColdInt = HotPath<HotPathTier_v::Cold, int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr HotInt h_default{};
static_assert(h_default.peek() == 0);
static_assert(h_default.tier == HotPathTier_v::Hot);

inline constexpr HotInt h_explicit{42};
static_assert(h_explicit.peek() == 42);

inline constexpr HotInt h_in_place{std::in_place, 7};
static_assert(h_in_place.peek() == 7);

// ── Pinned tier accessor ──────────────────────────────────────────
static_assert(HotInt::tier  == HotPathTier_v::Hot);
static_assert(WarmInt::tier == HotPathTier_v::Warm);
static_assert(ColdInt::tier == HotPathTier_v::Cold);

// ── satisfies<RequiredTier> — subsumption-up direction ────────────
//
// Hot satisfies every consumer.  THIS IS THE LOAD-BEARING POSITIVE
// TEST: Hot-pinned values pass every concept gate (`requires
// Subrow<callee_row, Row<HotPathTier::Hot>>`) — including the
// hot-path-only gates that the dispatcher will use to admit
// foreground operations.
static_assert(HotInt::satisfies<HotPathTier_v::Hot>);
static_assert(HotInt::satisfies<HotPathTier_v::Warm>);
static_assert(HotInt::satisfies<HotPathTier_v::Cold>);

// Warm satisfies Warm and Cold; FAILS on Hot.
static_assert( WarmInt::satisfies<HotPathTier_v::Warm>);    // self
static_assert( WarmInt::satisfies<HotPathTier_v::Cold>);    // weaker
static_assert(!WarmInt::satisfies<HotPathTier_v::Hot>,      // STRONGER fails ✓
    "Warm MUST NOT satisfy Hot — this is the load-bearing rejection "
    "that the hot-path dispatcher gates depend on.  If this fires, "
    "background work could silently flow into the foreground hot-path "
    "and miss the per-call shape budget (atomic ops + cache-line "
    "touches).");

// Cold satisfies only Cold.
static_assert( ColdInt::satisfies<HotPathTier_v::Cold>);
static_assert(!ColdInt::satisfies<HotPathTier_v::Warm>);
static_assert(!ColdInt::satisfies<HotPathTier_v::Hot>);

// ── relax<WeakerTier> — DOWN-the-lattice conversion ───────────────
inline constexpr auto from_hot_to_warm =
    HotInt{42}.relax<HotPathTier_v::Warm>();
static_assert(from_hot_to_warm.peek() == 42);
static_assert(from_hot_to_warm.tier == HotPathTier_v::Warm);

inline constexpr auto from_hot_to_cold =
    HotInt{99}.relax<HotPathTier_v::Cold>();
static_assert(from_hot_to_cold.peek() == 99);
static_assert(from_hot_to_cold.tier == HotPathTier_v::Cold);

inline constexpr auto from_warm_to_cold =
    WarmInt{7}.relax<HotPathTier_v::Cold>();
static_assert(from_warm_to_cold.peek() == 7);

inline constexpr auto from_warm_to_self =
    WarmInt{8}.relax<HotPathTier_v::Warm>();   // identity
static_assert(from_warm_to_self.peek() == 8);

// SFINAE-style detector — proves the requires-clause's correctness.
template <typename W, HotPathTier_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<HotInt,  HotPathTier_v::Warm>);    // ✓ down
static_assert( can_relax<HotInt,  HotPathTier_v::Cold>);    // ✓ down
static_assert( can_relax<HotInt,  HotPathTier_v::Hot>);     // ✓ self
static_assert( can_relax<WarmInt, HotPathTier_v::Cold>);    // ✓ down
static_assert( can_relax<WarmInt, HotPathTier_v::Warm>);    // ✓ self
static_assert(!can_relax<WarmInt, HotPathTier_v::Hot>,       // ✗ up
    "relax<Hot> on a Warm-pinned wrapper MUST be rejected — "
    "this is the load-bearing claim-stronger-than-source rejection. "
    "If this fires, the hot-path discipline can be silently bypassed "
    "by a Warm-tier value claiming Hot compliance.");
static_assert(!can_relax<ColdInt, HotPathTier_v::Warm>);    // ✗ up
static_assert(!can_relax<ColdInt, HotPathTier_v::Hot>);     // ✗ up
// Cold reflexivity — the bottom of the chain still admits relax to
// itself (leq is reflexive at every point including bottom).  Pinning
// this proves the requires-clause uses ≤ not strict-< at the chain
// endpoint.
static_assert( can_relax<ColdInt, HotPathTier_v::Cold>);    // ✓ self at bottom

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(HotInt::value_type_name().ends_with("int"));
static_assert(HotInt::lattice_name()  == "HotPathLattice::At<Hot>");
static_assert(WarmInt::lattice_name() == "HotPathLattice::At<Warm>");
static_assert(ColdInt::lattice_name() == "HotPathLattice::At<Cold>");

// ── swap exchanges T values within the same tier pin ─────────────
[[nodiscard]] consteval bool swap_exchanges_within_same_tier() noexcept {
    HotInt a{10};
    HotInt b{20};
    a.swap(b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(swap_exchanges_within_same_tier());

[[nodiscard]] consteval bool free_swap_works() noexcept {
    HotInt a{10};
    HotInt b{20};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10;
}
static_assert(free_swap_works());

// ── peek_mut allows in-place mutation ─────────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    HotInt a{10};
    a.peek_mut() = 99;
    return a.peek() == 99;
}
static_assert(peek_mut_works());

// ── operator== — same-tier, same-T comparison ─────────────────────
[[nodiscard]] consteval bool equality_compares_value_bytes() noexcept {
    HotInt a{42};
    HotInt b{42};
    HotInt c{43};
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

static_assert( can_equality_compare<HotInt>);
static_assert(!can_equality_compare<HotPath<HotPathTier_v::Hot, NoEqualityT>>);

// NoEqualityT has DELETED copy ctor — HotPath<Hot, NoEqualityT>
// must inherit that deletion.  Pins the structural property that
// T's move-only-ness propagates through the wrapper layer.
static_assert(!std::is_copy_constructible_v<HotPath<HotPathTier_v::Hot, NoEqualityT>>,
    "HotPath<Tier, T> must transitively inherit T's copy-deletion. "
    "If this fires, NoEqualityT's deleted copy ctor is no longer "
    "visible through the wrapper.");
static_assert(std::is_move_constructible_v<HotPath<HotPathTier_v::Hot, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    HotInt a{99};
    auto b = a.relax<HotPathTier_v::Hot>();
    return b.peek() == 99 && b.tier == HotPathTier_v::Hot;
}
static_assert(relax_to_self_is_identity());

// ── relax<>() && works on move-only T ─────────────────────────────
//
// Mirrors the DetSafe move-only-relax discipline.  The relax() &&
// overload moves T through `std::move(impl_).consume()` — does NOT
// require copy_constructible<T>.  Without this witness, a refactor
// accidentally adding `requires std::copy_constructible<T>` to the
// && overload would silently break Linear<T> and other move-only
// payloads.
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, HotPathTier_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, HotPathTier_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using HotMoveOnly = HotPath<HotPathTier_v::Hot, MoveOnlyT>;
static_assert( can_relax_rvalue<HotMoveOnly, HotPathTier_v::Warm>,
    "relax<>() && MUST work for move-only T — the rvalue overload "
    "moves through consume(), no copy required.");
static_assert(!can_relax_lvalue<HotMoveOnly, HotPathTier_v::Warm>,
    "relax<>() const& on move-only T MUST be rejected — the const& "
    "overload requires copy_constructible<T>.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    HotMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<HotPathTier_v::Warm>();
    return dst.peek().v == 77 && dst.tier == HotPathTier_v::Warm;
}
static_assert(relax_move_only_works());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(HotInt::value_type_name().size() > 0);
static_assert(HotInt::lattice_name().size() > 0);
static_assert(HotInt::lattice_name().starts_with("HotPathLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(hot_path::Hot<int>::tier  == HotPathTier_v::Hot);
static_assert(hot_path::Warm<int>::tier == HotPathTier_v::Warm);
static_assert(hot_path::Cold<int>::tier == HotPathTier_v::Cold);

static_assert(std::is_same_v<hot_path::Hot<double>,
                             HotPath<HotPathTier_v::Hot, double>>);

// ── Hot-path admission simulation — the load-bearing scenario ────
//
// This block simulates the dispatcher's hot-path admission gate
// (per 28_04 §4.3.2 + §6.4) at the concept level.  A function like
// TraceRing::try_push or Vessel::dispatch_op would have the
// requires-clause:
//   `requires HotPath<Tier, T>::satisfies<HotPathTier_v::Hot>`
// (or equivalently `requires (Tier == Hot)`).
//
// Below: the concept is_hot_path_admissible proves that
//   Hot-tier values PASS the gate (✓)
//   Warm-tier values are REJECTED (✓ — load-bearing)
//   Cold-tier values are REJECTED (✓)

template <typename W>
concept is_hot_path_admissible =
    W::template satisfies<HotPathTier_v::Hot>;

static_assert( is_hot_path_admissible<HotInt>,
    "Hot-tier value MUST pass the hot-path admission gate.");
static_assert(!is_hot_path_admissible<WarmInt>,
    "Warm-tier value MUST be REJECTED at the hot-path admission "
    "gate — this is the LOAD-BEARING TEST.  Without this rejection, "
    "background work flows silently into the foreground hot-path.");
static_assert(!is_hot_path_admissible<ColdInt>,
    "Cold-tier value MUST be REJECTED at the hot-path admission "
    "gate.");

// ── Runtime smoke test ─────────────────────────────────────────────
inline void runtime_smoke_test() {
    // Construction paths.
    HotInt a{};
    HotInt b{42};
    HotInt c{std::in_place, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();

    // Static tier accessor at runtime.
    if (HotInt::tier != HotPathTier_v::Hot) {
        std::abort();
    }

    // peek_mut.
    HotInt mutable_b{10};
    mutable_b.peek_mut() = 99;

    // Swap at runtime.
    HotInt sx{1};
    HotInt sy{2};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // relax<WeakerTier> — both overloads.
    HotInt source{77};
    auto relaxed_copy = source.relax<HotPathTier_v::Warm>();
    auto relaxed_move = std::move(source).relax<HotPathTier_v::Cold>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    // satisfies<...> at runtime.
    [[maybe_unused]] bool s1 = HotInt::satisfies<HotPathTier_v::Warm>;
    [[maybe_unused]] bool s2 = WarmInt::satisfies<HotPathTier_v::Hot>;

    // operator== — same-tier.
    HotInt eq_a{42};
    HotInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct from consumed inner.
    HotInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation.
    hot_path::Hot<int>  alias_hot{123};
    hot_path::Warm<int> alias_warm{456};
    hot_path::Cold<int> alias_cold{789};
    [[maybe_unused]] auto av = alias_hot.peek();
    [[maybe_unused]] auto wv = alias_warm.peek();
    [[maybe_unused]] auto cv = alias_cold.peek();

    // Hot-path admission simulation at runtime.
    [[maybe_unused]] bool can_hot_pass  = is_hot_path_admissible<HotInt>;
    [[maybe_unused]] bool can_warm_pass = is_hot_path_admissible<WarmInt>;
    [[maybe_unused]] bool can_cold_pass = is_hot_path_admissible<ColdInt>;
}

}  // namespace detail::hot_path_self_test

}  // namespace crucible::safety
