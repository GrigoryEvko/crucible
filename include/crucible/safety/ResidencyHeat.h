#pragma once

// ── crucible::safety::ResidencyHeat<ResidencyHeatTag_v Tier, T> ────
//
// Type-pinned working-set residency-heat (cache-tier) wrapper.  A
// value of type T whose cache-residency tier (Cold ⊑ Warm ⊑ Hot)
// is fixed at the type level via the non-type template parameter
// Tier.  Eighth chain wrapper from the 28_04_2026_effects.md §4.3.8
// catalog (FOUND-G49) — composes directly with the seven sister
// chain wrappers (HotPath / DetSafe / Wait / MemOrder / Progress /
// AllocClass / CipherTier) in canonical wrapper-nesting order
// (CLAUDE.md §XVI / FOUND-I03 — HotPath ⊃ DetSafe ⊃ ... ⊃
// ResidencyHeat ⊃ CipherTier ⊃ AllocClass ⊃ Wait ⊃ MemOrder ⊃ ...):
//
//   HotPath<Hot,
//       DetSafe<Pure,
//           ResidencyHeat<Hot,
//               CipherTier<Warm,
//                   AllocClass<Stack, T>>>>>
//
// Each layer EBO-collapses; the wrapper-nesting cost is sizeof(T)
// at -O3.  Per 28_04 §4.7 / CLAUDE.md §XVI: wrappers compose
// orthogonally in canonical order so row_hash
// (safety/diag/RowHashFold.h, FOUND-I02) folds deterministically;
// the dispatcher (FOUND-D) reads the stack via reflection.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     ResidencyHeatLattice::At<Tier>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//                 empty, sizeof(ResidencyHeat<Tier, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.8 + CRUCIBLE.md §L2 + §L15):
//     - KernelCache::publish_l1   → returns ResidencyHeat<Hot, T>
//                                    for IR002 hottest working-set
//     - KernelCache::publish_l2   → returns ResidencyHeat<Warm, T>
//                                    for IR003* per-vendor-family
//     - KernelCache::publish_l3   → returns ResidencyHeat<Cold, T>
//                                    for compiled-bytes archive
//     - Augur::sample_metric_hot   → ResidencyHeat<Hot, T>
//                                    for P95-window counters
//     - Augur::sample_metric_cold  → ResidencyHeat<Cold, T>
//                                    for long-window aggregates
//     - KernelCache::evict_to_warm → moves
//                                    ResidencyHeat<Hot> →
//                                    ResidencyHeat<Warm>
//
//   The bug class caught: a refactor that promotes a Cold-tier
//   (L3) value into a function expecting a Hot-tier (L1) input,
//   silently bypassing the working-set-residency contract.  Today
//   caught by perf regression (cold-tier load latency in a hot-path
//   loop); with the wrapper, becomes a compile error at the call
//   boundary because the function's `requires Hot` rejects
//   `ResidencyHeat<Cold>`.
//
//   ORTHOGONAL TO HotPath AND CipherTier:
//     - HotPath captures execution-budget tier (admitted operations).
//     - CipherTier captures storage-residency tier (durability —
//       RAM vs NVMe vs S3).
//     - ResidencyHeat captures cache-residency tier (working-set
//       heat — L1 vs L2 vs L3, Augur metric heat).
//   All three are 3-tier chains with Hot-at-top, all three
//   structurally identical, all three SEMANTICALLY DISTINCT.  A
//   single value may be HotPath<Hot> AND CipherTier<Warm> AND
//   ResidencyHeat<Hot> simultaneously: a foreground hot-path-safe
//   NVMe-backed value kept in the L1 IR002 hot working set.
//
//   Axiom coverage:
//     TypeSafe — ResidencyHeatTag_v is a strong scoped enum;
//                cross-tier mismatches are compile errors via the
//                relax<WeakerTier>() and satisfies<RequiredTier>
//                gates.
//     DetSafe — orthogonal axis; ResidencyHeat does NOT itself
//                enforce determinism.  Composes via wrapper-nesting.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(ResidencyHeat<Tier, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Tier>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A residency-heat pin is a STATIC property of WHERE the value is
// CACHED in the working set — not a context the value carries
// independent of its cache-tier.  The bytes themselves carry no
// information about their heat; the wrapper carries that
// information at the TYPE level.  Mirrors HotPath / CipherTier /
// DetSafe / AllocClass — all Absolute modalities over At<>-pinned
// grades.
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// ResidencyHeat subsumption-direction (per
// ResidencyHeatLattice.h docblock):
//
//   leq(weaker, stronger) reads "weaker-heat is below stronger-
//   heat in the lattice."
//   Bottom = Cold (weakest, slowest access — L3 / DRAM);
//   Top = Hot (strongest, fastest access — L1).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER tier (Hot) satisfies a consumer at a
//   WEAKER tier (Warm).  Stronger heat claim serves weaker
//   requirement.  A ResidencyHeat<Hot, T> can be relaxed to
//   ResidencyHeat<Warm, T> — the L1-resident value trivially
//   satisfies an L2-acceptance gate (eviction L1 → L2 is a
//   structural downgrade, not an upgrade).
//
//   The converse is forbidden: a ResidencyHeat<Cold, T> CANNOT
//   become a ResidencyHeat<Hot, T> — the cold-tier value lives in
//   L3; relaxing the type to claim Hot residency would defeat the
//   working-set discipline (the consumer assumes ~ns L1 access
//   but actually pays ~hundreds of ns L3 access).  No `tighten()`
//   method exists; the only way to obtain a ResidencyHeat<Hot, T>
//   is to construct one at a genuinely-L1-resident production
//   site (e.g., KernelCache::publish_l1, Augur::sample_metric_hot).
//
// API:
//
//   - relax<WeakerTier>() &  / && — convert to a less-strict tier;
//                                   compile error if WeakerTier >
//                                   Tier (would CLAIM more
//                                   residency-heat strength than
//                                   the source provides).
//   - satisfies<RequiredTier>     — static predicate: does this
//                                   wrapper's pinned tier subsume
//                                   the required tier?  Equivalent
//                                   to leq(RequiredTier, Tier).
//   - tier (static constexpr)     — the pinned ResidencyHeatTag_v.
//
// SEMANTIC NOTE on the "relax" naming: for ResidencyHeat,
// "weakening the tier" means accepting MORE permissive cache
// latency (going down the chain Cold ← Warm ← Hot).  Calling
// `relax<Cold>()` on a Hot-pinned value means "I'm OK treating
// this Hot value as Cold-tolerable here" — a downgrade of the
// cache-residency guarantee, not the value's bytes.  The API
// uses `relax` for uniformity with the seven sister chain
// wrappers.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has NO MEANINGFUL SEMANTICS for a
// type-pinned tier and would be the LOAD-BEARING BUG: a Cold-tier
// value claiming Hot residency would defeat the working-set
// discipline.  Hidden by the wrapper.
//
// See FOUND-G48 (algebra/lattices/ResidencyHeatLattice.h) for the
// underlying substrate; 28_04_2026_effects.md §4.3.8 + §4.7 for
// the production-call-site rationale and the canonical wrapper-
// nesting story; CRUCIBLE.md §L2 (KernelCache three-level cache)
// + §L15 (Augur metric heat) for the load-bearing consumers.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/ResidencyHeatLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the ResidencyHeatTag enum into the safety:: namespace under
// `ResidencyHeatTag_v`.  No name collision — the wrapper class is
// `ResidencyHeat`, not `ResidencyHeatTag`.  Naming convention
// matches HotPathTier_v + CipherTierTag_v from sibling wrappers.
using ::crucible::algebra::lattices::ResidencyHeatLattice;
using ResidencyHeatTag_v = ::crucible::algebra::lattices::ResidencyHeatTag;

template <ResidencyHeatTag_v Tier, typename T>
class [[nodiscard]] ResidencyHeat {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = ResidencyHeatLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned tier — exposed as a static constexpr for callers
    // doing tier-aware dispatch without instantiating the wrapper.
    static constexpr ResidencyHeatTag_v tier = Tier;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned tier.
    //
    // SEMANTIC NOTE: a default-constructed ResidencyHeat<Hot, T>
    // claims its T{} bytes were produced under L1-resident
    // discipline.  For trivially-zero T, this is vacuously true.
    // For non-trivial T or non-zero T{} in a populated context,
    // the claim becomes meaningful only if the wrapper is
    // constructed in a context that genuinely honors the tier
    // (e.g., a KernelCache::publish_l1 entry just placed in the
    // hottest 32 KB).  Production callers SHOULD prefer the
    // explicit-T constructor at tier-anchored production sites;
    // the default ctor exists for compatibility with
    // std::array<ResidencyHeat<Hot, T>, N> / struct-field
    // default-init contexts.
    constexpr ResidencyHeat() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a tier-anchored production site
    // constructs the wrapper at the appropriate tier.
    constexpr explicit ResidencyHeat(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit ResidencyHeat(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — ResidencyHeat IS COPYABLE within
    // the same tier pin.
    constexpr ResidencyHeat(const ResidencyHeat&)            = default;
    constexpr ResidencyHeat(ResidencyHeat&&)                 = default;
    constexpr ResidencyHeat& operator=(const ResidencyHeat&) = default;
    constexpr ResidencyHeat& operator=(ResidencyHeat&&)      = default;
    ~ResidencyHeat()                                         = default;

    // Equality: compares value bytes within the SAME tier pin.
    // Cross-tier comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        ResidencyHeat const& a, ResidencyHeat const& b) noexcept(
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
    constexpr void swap(ResidencyHeat& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(ResidencyHeat& a, ResidencyHeat& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredTier> — static subsumption check ────────
    //
    // True iff this wrapper's pinned tier is at least as strong as
    // RequiredTier.  Stronger tier (faster access, hotter cache)
    // satisfies weaker requirement (a Hot-pinned value is
    // admissible at a Warm-accepting consumer because it can be
    // demoted L1 → L2 losslessly via eviction).
    //
    // Use:
    //   static_assert(ResidencyHeat<ResidencyHeatTag_v::Hot, T>
    //                     ::satisfies<ResidencyHeatTag_v::Warm>);
    //   // ✓ — Hot subsumes Warm
    //
    //   static_assert(!ResidencyHeat<ResidencyHeatTag_v::Cold, T>
    //                      ::satisfies<ResidencyHeatTag_v::Hot>);
    //   // ✓ — Cold does NOT subsume Hot
    template <ResidencyHeatTag_v RequiredTier>
    static constexpr bool satisfies = ResidencyHeatLattice::leq(RequiredTier, Tier);

    // ── relax<WeakerTier> — convert to a less-strict tier ─────────
    //
    // Returns a ResidencyHeat<WeakerTier, T> carrying the same value
    // bytes.  Allowed iff WeakerTier ≤ Tier in the lattice (the
    // weaker tier is below or equal to the pinned tier).  Stronger
    // residency claim still serves weaker requirement.
    //
    // Compile error when WeakerTier > Tier — would CLAIM more
    // cache-residency strength than the source provides.
    template <ResidencyHeatTag_v WeakerTier>
        requires (ResidencyHeatLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr ResidencyHeat<WeakerTier, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return ResidencyHeat<WeakerTier, T>{this->peek()};
    }

    template <ResidencyHeatTag_v WeakerTier>
        requires (ResidencyHeatLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr ResidencyHeat<WeakerTier, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return ResidencyHeat<WeakerTier, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace residency_heat {
    template <typename T> using Hot  = ResidencyHeat<ResidencyHeatTag_v::Hot,  T>;
    template <typename T> using Warm = ResidencyHeat<ResidencyHeatTag_v::Warm, T>;
    template <typename T> using Cold = ResidencyHeat<ResidencyHeatTag_v::Cold, T>;
}  // namespace residency_heat

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::residency_heat_layout {

template <typename T> using HotR  = ResidencyHeat<ResidencyHeatTag_v::Hot,  T>;
template <typename T> using WarmR = ResidencyHeat<ResidencyHeatTag_v::Warm, T>;
template <typename T> using ColdR = ResidencyHeat<ResidencyHeatTag_v::Cold, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotR,  char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotR,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotR,  double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmR, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmR, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdR, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdR, double);

}  // namespace detail::residency_heat_layout

static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Hot,  int>)    == sizeof(int));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Warm, int>)    == sizeof(int));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Cold, int>)    == sizeof(int));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Hot,  double>) == sizeof(double));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Warm, double>) == sizeof(double));
static_assert(sizeof(ResidencyHeat<ResidencyHeatTag_v::Cold, double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::residency_heat_self_test {

using HotInt  = ResidencyHeat<ResidencyHeatTag_v::Hot,  int>;
using WarmInt = ResidencyHeat<ResidencyHeatTag_v::Warm, int>;
using ColdInt = ResidencyHeat<ResidencyHeatTag_v::Cold, int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr HotInt h_default{};
static_assert(h_default.peek() == 0);
static_assert(h_default.tier == ResidencyHeatTag_v::Hot);

inline constexpr HotInt h_explicit{42};
static_assert(h_explicit.peek() == 42);

inline constexpr HotInt h_in_place{std::in_place, 7};
static_assert(h_in_place.peek() == 7);

// ── Pinned tier accessor ──────────────────────────────────────────
static_assert(HotInt::tier  == ResidencyHeatTag_v::Hot);
static_assert(WarmInt::tier == ResidencyHeatTag_v::Warm);
static_assert(ColdInt::tier == ResidencyHeatTag_v::Cold);

// ── satisfies<RequiredTier> — subsumption-up direction ────────────
//
// Hot satisfies every consumer.  THIS IS THE LOAD-BEARING POSITIVE
// TEST: Hot-pinned values pass every concept gate (`requires
// Subrow<callee_row, Row<ResidencyHeatTag::Hot>>`) — including the
// L1-only gates that KernelCache fast-path lookups use.
static_assert(HotInt::satisfies<ResidencyHeatTag_v::Hot>);
static_assert(HotInt::satisfies<ResidencyHeatTag_v::Warm>);
static_assert(HotInt::satisfies<ResidencyHeatTag_v::Cold>);

// Warm satisfies Warm and Cold; FAILS on Hot.
static_assert( WarmInt::satisfies<ResidencyHeatTag_v::Warm>);    // self
static_assert( WarmInt::satisfies<ResidencyHeatTag_v::Cold>);    // weaker
static_assert(!WarmInt::satisfies<ResidencyHeatTag_v::Hot>,      // STRONGER fails ✓
    "Warm MUST NOT satisfy Hot — this is the load-bearing rejection "
    "that the KernelCache L1 admission gate depends on.  If this "
    "fires, an L2-resident value could be passed where an L1-resident "
    "value is required, leading to ~10× cache-miss latency in the "
    "fast-path lookup.");

// Cold satisfies only Cold.
static_assert( ColdInt::satisfies<ResidencyHeatTag_v::Cold>);
static_assert(!ColdInt::satisfies<ResidencyHeatTag_v::Warm>);
static_assert(!ColdInt::satisfies<ResidencyHeatTag_v::Hot>);

// ── relax<WeakerTier> — DOWN-the-lattice conversion ───────────────
inline constexpr auto from_hot_to_warm =
    HotInt{42}.relax<ResidencyHeatTag_v::Warm>();
static_assert(from_hot_to_warm.peek() == 42);
static_assert(from_hot_to_warm.tier == ResidencyHeatTag_v::Warm);

inline constexpr auto from_hot_to_cold =
    HotInt{99}.relax<ResidencyHeatTag_v::Cold>();
static_assert(from_hot_to_cold.peek() == 99);
static_assert(from_hot_to_cold.tier == ResidencyHeatTag_v::Cold);

inline constexpr auto from_warm_to_cold =
    WarmInt{7}.relax<ResidencyHeatTag_v::Cold>();
static_assert(from_warm_to_cold.peek() == 7);

inline constexpr auto from_warm_to_self =
    WarmInt{8}.relax<ResidencyHeatTag_v::Warm>();   // identity
static_assert(from_warm_to_self.peek() == 8);

// SFINAE-style detector — proves the requires-clause's correctness.
template <typename W, ResidencyHeatTag_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<HotInt,  ResidencyHeatTag_v::Warm>);    // ✓ down
static_assert( can_relax<HotInt,  ResidencyHeatTag_v::Cold>);    // ✓ down
static_assert( can_relax<HotInt,  ResidencyHeatTag_v::Hot>);     // ✓ self
static_assert( can_relax<WarmInt, ResidencyHeatTag_v::Cold>);    // ✓ down
static_assert( can_relax<WarmInt, ResidencyHeatTag_v::Warm>);    // ✓ self
static_assert(!can_relax<WarmInt, ResidencyHeatTag_v::Hot>,       // ✗ up
    "relax<Hot> on a Warm-pinned wrapper MUST be rejected — "
    "this is the load-bearing claim-stronger-than-source rejection. "
    "If this fires, an L2-resident value could silently claim "
    "L1-residency, defeating the working-set discipline (consumers "
    "would assume ~ns access but actually pay ~tens of ns).");
static_assert(!can_relax<ColdInt, ResidencyHeatTag_v::Warm>);    // ✗ up
static_assert(!can_relax<ColdInt, ResidencyHeatTag_v::Hot>);     // ✗ up
// Cold reflexivity — the bottom of the chain still admits relax to
// itself (leq is reflexive at every point including bottom).
static_assert( can_relax<ColdInt, ResidencyHeatTag_v::Cold>);    // ✓ self at bottom

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(HotInt::value_type_name().ends_with("int"));
static_assert(HotInt::lattice_name()  == "ResidencyHeatLattice::At<Hot>");
static_assert(WarmInt::lattice_name() == "ResidencyHeatLattice::At<Warm>");
static_assert(ColdInt::lattice_name() == "ResidencyHeatLattice::At<Cold>");

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
static_assert(!can_equality_compare<ResidencyHeat<ResidencyHeatTag_v::Hot, NoEqualityT>>);

// NoEqualityT has DELETED copy ctor — ResidencyHeat<Hot, NoEqualityT>
// must inherit that deletion.  Pins the structural property that
// T's move-only-ness propagates through the wrapper layer.
static_assert(!std::is_copy_constructible_v<ResidencyHeat<ResidencyHeatTag_v::Hot, NoEqualityT>>,
    "ResidencyHeat<Tier, T> must transitively inherit T's copy-deletion. "
    "If this fires, NoEqualityT's deleted copy ctor is no longer "
    "visible through the wrapper.");
static_assert(std::is_move_constructible_v<ResidencyHeat<ResidencyHeatTag_v::Hot, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    HotInt a{99};
    auto b = a.relax<ResidencyHeatTag_v::Hot>();
    return b.peek() == 99 && b.tier == ResidencyHeatTag_v::Hot;
}
static_assert(relax_to_self_is_identity());

// ── relax<>() && works on move-only T ─────────────────────────────
//
// Mirrors the seven sister chain wrappers' move-only-relax
// discipline.  Without this witness, a refactor accidentally
// adding `requires std::copy_constructible<T>` to the && overload
// would silently break Linear<T> and other move-only payloads.
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, ResidencyHeatTag_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, ResidencyHeatTag_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using HotMoveOnly = ResidencyHeat<ResidencyHeatTag_v::Hot, MoveOnlyT>;
static_assert( can_relax_rvalue<HotMoveOnly, ResidencyHeatTag_v::Warm>,
    "relax<>() && MUST work for move-only T — the rvalue overload "
    "moves through consume(), no copy required.");
static_assert(!can_relax_lvalue<HotMoveOnly, ResidencyHeatTag_v::Warm>,
    "relax<>() const& on move-only T MUST be rejected — the const& "
    "overload requires copy_constructible<T>.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    HotMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<ResidencyHeatTag_v::Warm>();
    return dst.peek().v == 77 && dst.tier == ResidencyHeatTag_v::Warm;
}
static_assert(relax_move_only_works());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(HotInt::value_type_name().size() > 0);
static_assert(HotInt::lattice_name().size() > 0);
static_assert(HotInt::lattice_name().starts_with("ResidencyHeatLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(residency_heat::Hot<int>::tier  == ResidencyHeatTag_v::Hot);
static_assert(residency_heat::Warm<int>::tier == ResidencyHeatTag_v::Warm);
static_assert(residency_heat::Cold<int>::tier == ResidencyHeatTag_v::Cold);

static_assert(std::is_same_v<residency_heat::Hot<double>,
                             ResidencyHeat<ResidencyHeatTag_v::Hot, double>>);

// ── L1 fast-path admission simulation — load-bearing scenario ────
//
// This block simulates KernelCache's L1 fast-path admission gate
// (per 28_04 §4.3.8 + CRUCIBLE.md §L2).  KernelCache's hottest
// 32 KB L1 IR002 slots admit only ResidencyHeat<Hot> entries —
// reading from L2 (Warm) costs ~10× more cycles and breaks the
// per-call shape budget the dispatcher relies on.
//
// Below: the concept is_l1_admissible proves that
//   Hot-tier values PASS the gate (✓)
//   Warm-tier values are REJECTED (✓ — load-bearing, would force
//                                   L2 access in the fast path)
//   Cold-tier values are REJECTED (✓)

template <typename W>
concept is_l1_admissible =
    W::template satisfies<ResidencyHeatTag_v::Hot>;

static_assert( is_l1_admissible<HotInt>,
    "Hot-tier value MUST pass the KernelCache L1 admission gate.");
static_assert(!is_l1_admissible<WarmInt>,
    "Warm-tier value MUST be REJECTED at the L1 admission gate — "
    "this is the LOAD-BEARING TEST.  Without this rejection, an "
    "L2-resident slot could enter the L1 fast path and pay ~10× "
    "the per-call latency the dispatcher's working-set budget assumes.");
static_assert(!is_l1_admissible<ColdInt>,
    "Cold-tier value MUST be REJECTED at the L1 admission gate.");

// ── Warm-or-better cache lookup admission simulation ────────────
//
// A second admission simulation: KernelCache::lookup_warm
// requires ResidencyHeat<Warm-or-better>.  The full slow-path
// L3 lookup is a separate API; the warm path is for the typical
// "hot working set + occasional L2 spill" pattern.

template <typename W>
concept is_warm_lookup_admissible =
    W::template satisfies<ResidencyHeatTag_v::Warm>;

static_assert( is_warm_lookup_admissible<HotInt>,
    "Hot-tier value MUST pass the KernelCache warm-lookup admission "
    "gate (Hot subsumes Warm — eviction L1 → L2 is a valid downgrade).");
static_assert( is_warm_lookup_admissible<WarmInt>,
    "Warm-tier value MUST pass the KernelCache warm-lookup admission "
    "gate (self-admission).");
static_assert(!is_warm_lookup_admissible<ColdInt>,
    "Cold-tier value MUST be REJECTED at the KernelCache warm-lookup "
    "admission gate — Cold is BELOW Warm in the chain; allowing a "
    "Cold-tier value into the warm path would defeat the working-set "
    "discipline that Augur's heat tracking depends on.");

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
    if (HotInt::tier != ResidencyHeatTag_v::Hot) {
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
    auto relaxed_copy = source.relax<ResidencyHeatTag_v::Warm>();
    auto relaxed_move = std::move(source).relax<ResidencyHeatTag_v::Cold>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    // satisfies<...> at runtime.
    [[maybe_unused]] bool s1 = HotInt::satisfies<ResidencyHeatTag_v::Warm>;
    [[maybe_unused]] bool s2 = WarmInt::satisfies<ResidencyHeatTag_v::Hot>;

    // operator== — same-tier.
    HotInt eq_a{42};
    HotInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct from consumed inner.
    HotInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation.
    residency_heat::Hot<int>  alias_hot{123};
    residency_heat::Warm<int> alias_warm{456};
    residency_heat::Cold<int> alias_cold{789};
    [[maybe_unused]] auto av = alias_hot.peek();
    [[maybe_unused]] auto wv = alias_warm.peek();
    [[maybe_unused]] auto cv = alias_cold.peek();

    // L1 + warm-lookup admission simulations at runtime.
    [[maybe_unused]] bool can_l1   = is_l1_admissible<HotInt>;
    [[maybe_unused]] bool can_warm = is_warm_lookup_admissible<HotInt>;
}

}  // namespace detail::residency_heat_self_test

}  // namespace crucible::safety
