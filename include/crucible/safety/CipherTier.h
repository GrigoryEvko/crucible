#pragma once

// ── crucible::safety::CipherTier<CipherTierTag_v Tier, T> ───────────
//
// Type-pinned Cipher persistence-residency wrapper.  A value of type
// T whose Cipher tier (Cold ⊑ Warm ⊑ Hot) is fixed at the type level
// via the non-type template parameter Tier.  Seventh chain wrapper
// from the 28_04_2026_effects.md §4.3.7 catalog (FOUND-G44) — composes
// directly with HotPath / DetSafe / AllocClass in canonical wrapper-
// nesting order (CLAUDE.md §XVI / FOUND-I03 — HotPath ⊃ DetSafe ⊃
// ... ⊃ CipherTier ⊃ AllocClass):
//
//   HotPath<Warm,
//       DetSafe<DetCheap,
//           CipherTier<Hot,
//               AllocClass<Pool, T>>>>
//
// Each layer EBO-collapses; the wrapper-nesting cost is sizeof(T)
// at -O3.  Per 28_04 §4.7 / CLAUDE.md §XVI: wrappers compose
// orthogonally in canonical order so row_hash
// (safety/diag/RowHashFold.h, FOUND-I02) folds deterministically;
// the dispatcher (FOUND-D) reads the stack via reflection.
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     CipherTierLattice::At<Tier>,
//                     T>
//   Regime:    1 (zero-cost EBO collapse — At<Tier>::element_type is
//                 empty, sizeof(CipherTier<Tier, T>) == sizeof(T))
//
//   Use cases (per 28_04 §4.3.7 + CRUCIBLE.md §L14):
//     - Cipher::publish_hot    → returns CipherTier<Hot, T>
//     - Cipher::publish_warm   → returns CipherTier<Warm, T>
//     - Cipher::flush_cold     → returns CipherTier<Cold, T>
//     - Augur::attribute_drift  → reads tier to distinguish "S3 latency"
//                                  from "hot-tier issue"
//     - Keeper::reincarnate    → requires CipherTier<Warm-or-better>
//                                  for partial recovery (Hot tier
//                                  preferred for zero-loss reshard)
//     - KernelCache::evict     → moves CipherTier<Warm> → CipherTier<Cold>
//     - replay_engine          → ingests CipherTier<Cold> historical
//                                  archives at recovery time
//
//   The bug class caught: a refactor that publishes into a hotter
//   tier than the value actually lives in, e.g., calling
//   `register_hot(value)` on a value loaded from S3 cold storage.
//   Today caught by review or recovery-time corruption; with the
//   wrapper, becomes a compile error at the call boundary because
//   the function's `requires Hot` rejects a `CipherTier<Cold>` value.
//
//   ORTHOGONAL TO HotPath: HotPath captures execution-budget tier
//   ("can I do alloc here?"); CipherTier captures storage-residency
//   tier ("how fast can I recover this on failure?").  Same 3-tier
//   shape, distinct semantic axis.  A function may be HotPath<Hot>
//   AND publish CipherTier<Cold> (e.g., "fast classifier that
//   schedules a cold-tier archive write").
//
//   Axiom coverage:
//     TypeSafe — CipherTierTag_v is a strong scoped enum;
//                cross-tier mismatches are compile errors via the
//                relax<WeakerTier>() and satisfies<RequiredTier>
//                gates.
//     DetSafe — orthogonal axis; CipherTier does NOT itself enforce
//                determinism.  Composes via wrapper-nesting.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//     InitSafe — NSDMI on impl_ via Graded's substrate.
//   Runtime cost:
//     sizeof(CipherTier<Tier, T>) == sizeof(T).  Verified by
//     CRUCIBLE_GRADED_LAYOUT_INVARIANT below.  At<Tier>::element_type
//     is empty; Graded's [[no_unique_address]] grade_ EBO-collapses;
//     the wrapper is byte-equivalent to the bare T at -O3.
//
// ── Why Modality::Absolute ─────────────────────────────────────────
//
// A Cipher tier pin is a STATIC property of WHERE the value LIVES
// in the persistence hierarchy — not a context the value carries
// independent of its storage.  The bytes themselves carry no
// information about their tier; the wrapper carries that information
// at the TYPE level.  Mirrors HotPath (execution-budget pin),
// DetSafe (determinism-safety pin), AllocClass (allocator-class
// pin) — all Absolute modalities over At<>-pinned grades.
//
// ── Tier-conversion API: relax + satisfies ─────────────────────────
//
// CipherTier subsumption-direction (per CipherTierLattice.h
// docblock):
//
//   leq(weaker, stronger) reads "weaker-tier is below stronger-tier
//   in the lattice."
//   Bottom = Cold (weakest persistence-recovery claim);
//   Top = Hot (strongest, fastest recovery).
//
// For USE, the direction is REVERSED:
//
//   A producer at a STRONGER tier (Hot) satisfies a consumer at a
//   WEAKER tier (Warm).  Stronger persistence claim serves weaker
//   requirement.  A CipherTier<Hot, T> can be relaxed to
//   CipherTier<Warm, T> — the Hot-tier value trivially satisfies
//   the Warm-acceptance gate (replicating Hot-RAM contents to a
//   Warm-NVMe consumer is a structural downgrade, not an upgrade
//   of any guarantee).
//
//   The converse is forbidden: a CipherTier<Cold, T> CANNOT become
//   a CipherTier<Hot, T> — the cold-tier value lives only in
//   durable storage; relaxing the type to claim Hot residency
//   would defeat the persistence-tier discipline (a Cipher operation
//   downstream might assume it's reading from RAM and never wait
//   on the actual S3 GET that's needed).  No `tighten()` method
//   exists; the only way to obtain a CipherTier<Hot, T> is to
//   construct one at a genuinely-Hot-tier production site (e.g.,
//   Cipher::publish_hot, Keeper::receive_replicated).
//
// API:
//
//   - relax<WeakerTier>() &  / && — convert to a less-strict tier;
//                                   compile error if WeakerTier >
//                                   Tier (would CLAIM more
//                                   persistence-recovery strength
//                                   than the source provides).
//   - satisfies<RequiredTier>     — static predicate: does this
//                                   wrapper's pinned tier subsume
//                                   the required tier?  Equivalent
//                                   to leq(RequiredTier, Tier).
//   - tier (static constexpr)     — the pinned CipherTierTag_v value.
//
// SEMANTIC NOTE on the "relax" naming: for CipherTier, "weakening
// the tier" means accepting MORE permissive recovery latencies
// (going down the chain Cold ← Warm ← Hot).  Calling `relax<Cold>()`
// on a Hot-pinned value means "I'm OK treating this Hot value as
// Cold-tolerable here" — a downgrade of the recovery guarantee,
// not the value's bytes.  The API uses `relax` for uniformity with
// HotPath / DetSafe / Wait / MemOrder / Progress / AllocClass.
//
// `Graded::weaken` on the substrate goes UP the lattice (stronger
// promise) — that operation has NO MEANINGFUL SEMANTICS for a
// type-pinned tier and would be the LOAD-BEARING BUG: a Cold-tier
// value claiming Hot residency would defeat the persistence
// discipline.  Hidden by the wrapper.
//
// See FOUND-G43 (algebra/lattices/CipherTierLattice.h) for the
// underlying substrate; 28_04_2026_effects.md §4.3.7 + §4.7 for
// the production-call-site rationale and the canonical wrapper-
// nesting story; CRUCIBLE.md §L14 for the Cipher three-tier
// persistence architecture.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/CipherTierLattice.h>

#include <cstdlib>      // std::abort in the runtime smoke test
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the CipherTierTag enum into the safety:: namespace under
// `CipherTierTag_v`.  No name collision — the wrapper class is
// `CipherTier`, not `CipherTierTag`.  Naming convention matches
// HotPathTier_v + AllocClassTag_v from sibling wrappers.
using ::crucible::algebra::lattices::CipherTierLattice;
using CipherTierTag_v = ::crucible::algebra::lattices::CipherTierTag;

template <CipherTierTag_v Tier, typename T>
class [[nodiscard]] CipherTier {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = CipherTierLattice::At<Tier>;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

    // The pinned tier — exposed as a static constexpr for callers
    // doing tier-aware dispatch without instantiating the wrapper.
    static constexpr CipherTierTag_v tier = Tier;

private:
    graded_type impl_;

public:

    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at the pinned tier.
    //
    // SEMANTIC NOTE: a default-constructed CipherTier<Hot, T> claims
    // its T{} bytes were produced under Hot-tier residency (i.e.,
    // the value lives in another Relay's RAM at the moment the
    // wrapper is constructed).  For trivially-zero T, this is
    // vacuously true.  For non-trivial T or non-zero T{} in a
    // populated context, the claim becomes meaningful only if the
    // wrapper is constructed in a context that genuinely honors the
    // tier (e.g., a replication-receive-buffer in Keeper's hot path,
    // not a stack-local helper).  Production callers SHOULD prefer
    // the explicit-T constructor at tier-anchored production sites;
    // the default ctor exists for compatibility with
    // std::array<CipherTier<Hot, T>, N> / struct-field default-init.
    constexpr CipherTier() noexcept(
        std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, typename lattice_type::element_type{}} {}

    // Explicit construction from a T value.  The most common
    // production pattern — a tier-anchored production site
    // constructs the wrapper at the appropriate tier.
    constexpr explicit CipherTier(T value) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), typename lattice_type::element_type{}} {}

    // In-place construction.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr explicit CipherTier(std::in_place_t, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...),
                typename lattice_type::element_type{}} {}

    // Defaulted copy/move/destroy — CipherTier IS COPYABLE within
    // the same tier pin.
    constexpr CipherTier(const CipherTier&)            = default;
    constexpr CipherTier(CipherTier&&)                 = default;
    constexpr CipherTier& operator=(const CipherTier&) = default;
    constexpr CipherTier& operator=(CipherTier&&)      = default;
    ~CipherTier()                                      = default;

    // Equality: compares value bytes within the SAME tier pin.
    // Cross-tier comparison rejected at overload resolution.
    [[nodiscard]] friend constexpr bool operator==(
        CipherTier const& a, CipherTier const& b) noexcept(
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
    constexpr void swap(CipherTier& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(CipherTier& a, CipherTier& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── satisfies<RequiredTier> — static subsumption check ────────
    //
    // True iff this wrapper's pinned tier is at least as strong as
    // RequiredTier.  Stronger tier (faster recovery) satisfies
    // weaker requirement (a Hot-pinned value is admissible at a
    // Warm-accepting consumer because it can be downgraded losslessly
    // to NVMe persistence).
    //
    // Use:
    //   static_assert(CipherTier<CipherTierTag_v::Hot, T>
    //                     ::satisfies<CipherTierTag_v::Warm>);
    //   // ✓ — Hot subsumes Warm
    //
    //   static_assert(!CipherTier<CipherTierTag_v::Cold, T>
    //                      ::satisfies<CipherTierTag_v::Hot>);
    //   // ✓ — Cold does NOT subsume Hot
    template <CipherTierTag_v RequiredTier>
    static constexpr bool satisfies = CipherTierLattice::leq(RequiredTier, Tier);

    // ── relax<WeakerTier> — convert to a less-strict tier ─────────
    //
    // Returns a CipherTier<WeakerTier, T> carrying the same value
    // bytes.  Allowed iff WeakerTier ≤ Tier in the lattice (the
    // weaker tier is below or equal to the pinned tier).  Stronger
    // persistence claim still serves weaker requirement.
    //
    // Compile error when WeakerTier > Tier — would CLAIM more
    // persistence-recovery strength than the source provides.
    template <CipherTierTag_v WeakerTier>
        requires (CipherTierLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr CipherTier<WeakerTier, T> relax() const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return CipherTier<WeakerTier, T>{this->peek()};
    }

    template <CipherTierTag_v WeakerTier>
        requires (CipherTierLattice::leq(WeakerTier, Tier))
    [[nodiscard]] constexpr CipherTier<WeakerTier, T> relax() &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return CipherTier<WeakerTier, T>{
            std::move(impl_).consume()};
    }
};

// ── Convenience aliases ─────────────────────────────────────────────
namespace cipher_tier {
    template <typename T> using Hot  = CipherTier<CipherTierTag_v::Hot,  T>;
    template <typename T> using Warm = CipherTier<CipherTierTag_v::Warm, T>;
    template <typename T> using Cold = CipherTier<CipherTierTag_v::Cold, T>;
}  // namespace cipher_tier

// ── Layout invariants ───────────────────────────────────────────────
namespace detail::cipher_tier_layout {

template <typename T> using HotC  = CipherTier<CipherTierTag_v::Hot,  T>;
template <typename T> using WarmC = CipherTier<CipherTierTag_v::Warm, T>;
template <typename T> using ColdC = CipherTier<CipherTierTag_v::Cold, T>;

CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotC,  char);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotC,  int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(HotC,  double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(WarmC, double);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdC, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(ColdC, double);

}  // namespace detail::cipher_tier_layout

static_assert(sizeof(CipherTier<CipherTierTag_v::Hot,  int>)    == sizeof(int));
static_assert(sizeof(CipherTier<CipherTierTag_v::Warm, int>)    == sizeof(int));
static_assert(sizeof(CipherTier<CipherTierTag_v::Cold, int>)    == sizeof(int));
static_assert(sizeof(CipherTier<CipherTierTag_v::Hot,  double>) == sizeof(double));
static_assert(sizeof(CipherTier<CipherTierTag_v::Warm, double>) == sizeof(double));
static_assert(sizeof(CipherTier<CipherTierTag_v::Cold, double>) == sizeof(double));

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::cipher_tier_self_test {

using HotInt  = CipherTier<CipherTierTag_v::Hot,  int>;
using WarmInt = CipherTier<CipherTierTag_v::Warm, int>;
using ColdInt = CipherTier<CipherTierTag_v::Cold, int>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr HotInt h_default{};
static_assert(h_default.peek() == 0);
static_assert(h_default.tier == CipherTierTag_v::Hot);

inline constexpr HotInt h_explicit{42};
static_assert(h_explicit.peek() == 42);

inline constexpr HotInt h_in_place{std::in_place, 7};
static_assert(h_in_place.peek() == 7);

// ── Pinned tier accessor ──────────────────────────────────────────
static_assert(HotInt::tier  == CipherTierTag_v::Hot);
static_assert(WarmInt::tier == CipherTierTag_v::Warm);
static_assert(ColdInt::tier == CipherTierTag_v::Cold);

// ── satisfies<RequiredTier> — subsumption-up direction ────────────
//
// Hot satisfies every consumer.  THIS IS THE LOAD-BEARING POSITIVE
// TEST: Hot-pinned values pass every concept gate (`requires
// Subrow<callee_row, Row<CipherTierTag::Hot>>`) — including the
// hot-tier-only gates that Keeper's reincarnation path uses to
// admit zero-loss reshard.
static_assert(HotInt::satisfies<CipherTierTag_v::Hot>);
static_assert(HotInt::satisfies<CipherTierTag_v::Warm>);
static_assert(HotInt::satisfies<CipherTierTag_v::Cold>);

// Warm satisfies Warm and Cold; FAILS on Hot.
static_assert( WarmInt::satisfies<CipherTierTag_v::Warm>);    // self
static_assert( WarmInt::satisfies<CipherTierTag_v::Cold>);    // weaker
static_assert(!WarmInt::satisfies<CipherTierTag_v::Hot>,      // STRONGER fails ✓
    "Warm MUST NOT satisfy Hot — this is the load-bearing rejection "
    "that the Keeper hot-tier reshard gates depend on.  If this fires, "
    "an NVMe-backed value could be passed where a RAM-replicated value "
    "is required, leading to blocking-disk-IO at the worst possible "
    "moment (cluster-failure recovery fan-in).");

// Cold satisfies only Cold.
static_assert( ColdInt::satisfies<CipherTierTag_v::Cold>);
static_assert(!ColdInt::satisfies<CipherTierTag_v::Warm>);
static_assert(!ColdInt::satisfies<CipherTierTag_v::Hot>);

// ── relax<WeakerTier> — DOWN-the-lattice conversion ───────────────
inline constexpr auto from_hot_to_warm =
    HotInt{42}.relax<CipherTierTag_v::Warm>();
static_assert(from_hot_to_warm.peek() == 42);
static_assert(from_hot_to_warm.tier == CipherTierTag_v::Warm);

inline constexpr auto from_hot_to_cold =
    HotInt{99}.relax<CipherTierTag_v::Cold>();
static_assert(from_hot_to_cold.peek() == 99);
static_assert(from_hot_to_cold.tier == CipherTierTag_v::Cold);

inline constexpr auto from_warm_to_cold =
    WarmInt{7}.relax<CipherTierTag_v::Cold>();
static_assert(from_warm_to_cold.peek() == 7);

inline constexpr auto from_warm_to_self =
    WarmInt{8}.relax<CipherTierTag_v::Warm>();   // identity
static_assert(from_warm_to_self.peek() == 8);

// SFINAE-style detector — proves the requires-clause's correctness.
template <typename W, CipherTierTag_v T_target>
concept can_relax = requires(W w) {
    { std::move(w).template relax<T_target>() };
};

static_assert( can_relax<HotInt,  CipherTierTag_v::Warm>);    // ✓ down
static_assert( can_relax<HotInt,  CipherTierTag_v::Cold>);    // ✓ down
static_assert( can_relax<HotInt,  CipherTierTag_v::Hot>);     // ✓ self
static_assert( can_relax<WarmInt, CipherTierTag_v::Cold>);    // ✓ down
static_assert( can_relax<WarmInt, CipherTierTag_v::Warm>);    // ✓ self
static_assert(!can_relax<WarmInt, CipherTierTag_v::Hot>,       // ✗ up
    "relax<Hot> on a Warm-pinned wrapper MUST be rejected — "
    "this is the load-bearing claim-stronger-than-source rejection. "
    "If this fires, a CipherTier<Cold> value could silently claim "
    "Hot-tier residency, defeating the persistence-tier discipline "
    "(downstream Cipher operations would never wait on the actual "
    "S3/NVMe IO required to materialize the value).");
static_assert(!can_relax<ColdInt, CipherTierTag_v::Warm>);    // ✗ up
static_assert(!can_relax<ColdInt, CipherTierTag_v::Hot>);     // ✗ up
// Cold reflexivity — the bottom of the chain still admits relax to
// itself (leq is reflexive at every point including bottom).  Pinning
// this proves the requires-clause uses ≤ not strict-< at the chain
// endpoint.
static_assert( can_relax<ColdInt, CipherTierTag_v::Cold>);    // ✓ self at bottom

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(HotInt::value_type_name().ends_with("int"));
static_assert(HotInt::lattice_name()  == "CipherTierLattice::At<Hot>");
static_assert(WarmInt::lattice_name() == "CipherTierLattice::At<Warm>");
static_assert(ColdInt::lattice_name() == "CipherTierLattice::At<Cold>");

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
static_assert(!can_equality_compare<CipherTier<CipherTierTag_v::Hot, NoEqualityT>>);

// NoEqualityT has DELETED copy ctor — CipherTier<Hot, NoEqualityT>
// must inherit that deletion.  Pins the structural property that
// T's move-only-ness propagates through the wrapper layer.
static_assert(!std::is_copy_constructible_v<CipherTier<CipherTierTag_v::Hot, NoEqualityT>>,
    "CipherTier<Tier, T> must transitively inherit T's copy-deletion. "
    "If this fires, NoEqualityT's deleted copy ctor is no longer "
    "visible through the wrapper.");
static_assert(std::is_move_constructible_v<CipherTier<CipherTierTag_v::Hot, NoEqualityT>>);

// ── relax reflexivity ─────────────────────────────────────────────
[[nodiscard]] consteval bool relax_to_self_is_identity() noexcept {
    HotInt a{99};
    auto b = a.relax<CipherTierTag_v::Hot>();
    return b.peek() == 99 && b.tier == CipherTierTag_v::Hot;
}
static_assert(relax_to_self_is_identity());

// ── relax<>() && works on move-only T ─────────────────────────────
//
// Mirrors the HotPath move-only-relax discipline.  The relax() &&
// overload moves T through `std::move(impl_).consume()` — does NOT
// require copy_constructible<T>.  Without this witness, a refactor
// accidentally adding `requires std::copy_constructible<T>` to the
// && overload would silently break Linear<T> and other move-only
// payloads — including the Cipher tier-reassignment path (which
// moves a unique-owned shard between tiers without copying).
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

template <typename W, CipherTierTag_v T_target>
concept can_relax_rvalue = requires(W&& w) {
    { std::move(w).template relax<T_target>() };
};
template <typename W, CipherTierTag_v T_target>
concept can_relax_lvalue = requires(W const& w) {
    { w.template relax<T_target>() };
};

using HotMoveOnly = CipherTier<CipherTierTag_v::Hot, MoveOnlyT>;
static_assert( can_relax_rvalue<HotMoveOnly, CipherTierTag_v::Warm>,
    "relax<>() && MUST work for move-only T — the rvalue overload "
    "moves through consume(), no copy required.");
static_assert(!can_relax_lvalue<HotMoveOnly, CipherTierTag_v::Warm>,
    "relax<>() const& on move-only T MUST be rejected — the const& "
    "overload requires copy_constructible<T>.");

[[nodiscard]] consteval bool relax_move_only_works() noexcept {
    HotMoveOnly src{MoveOnlyT{77}};
    auto dst = std::move(src).relax<CipherTierTag_v::Warm>();
    return dst.peek().v == 77 && dst.tier == CipherTierTag_v::Warm;
}
static_assert(relax_move_only_works());

// ── Stable-name introspection (FOUND-E07/H06 surface) ────────────
static_assert(HotInt::value_type_name().size() > 0);
static_assert(HotInt::lattice_name().size() > 0);
static_assert(HotInt::lattice_name().starts_with("CipherTierLattice::At<"));

// ── Convenience aliases resolve correctly ────────────────────────
static_assert(cipher_tier::Hot<int>::tier  == CipherTierTag_v::Hot);
static_assert(cipher_tier::Warm<int>::tier == CipherTierTag_v::Warm);
static_assert(cipher_tier::Cold<int>::tier == CipherTierTag_v::Cold);

static_assert(std::is_same_v<cipher_tier::Hot<double>,
                             CipherTier<CipherTierTag_v::Hot, double>>);

// ── Hot-tier reshard admission simulation — load-bearing scenario ─
//
// This block simulates Keeper's hot-tier reincarnation admission
// gate (per 28_04 §4.3.7 + CRUCIBLE.md §L14).  When a Relay dies,
// fellow Relays already have RAM-replicated shards (RAID-style hot
// tier).  The reincarnation fast path requires CipherTier<Hot> —
// reading from NVMe (Warm) would block on disk IO at the worst
// possible moment, and reading from S3 (Cold) is minutes-of-latency
// catastrophic.
//
// Below: the concept is_hot_reshard_admissible proves that
//   Hot-tier values PASS the gate (✓)
//   Warm-tier values are REJECTED (✓ — load-bearing, would force
//                                   blocking NVMe IO under
//                                   recovery-time pressure)
//   Cold-tier values are REJECTED (✓)

template <typename W>
concept is_hot_reshard_admissible =
    W::template satisfies<CipherTierTag_v::Hot>;

static_assert( is_hot_reshard_admissible<HotInt>,
    "Hot-tier value MUST pass the Keeper hot-reshard admission gate.");
static_assert(!is_hot_reshard_admissible<WarmInt>,
    "Warm-tier value MUST be REJECTED at the hot-reshard admission "
    "gate — this is the LOAD-BEARING TEST.  Without this rejection, "
    "the recovery path could silently block on NVMe IO when zero-cost "
    "RAM replication was the contract.");
static_assert(!is_hot_reshard_admissible<ColdInt>,
    "Cold-tier value MUST be REJECTED at the hot-reshard admission "
    "gate.");

// ── Warm-or-better publish admission simulation — second scenario ─
//
// A second admission simulation: Cipher::publish_warm requires
// CipherTier<Warm-or-better>.  Captures the Cipher publication
// boundary that distinguishes hot-tier broadcast (Hot) and
// per-iteration NVMe checkpoint (Warm) from cold-tier eviction
// (Cold-only is rejected — you can't "publish" something that
// already lives in cold storage; the publish-warm path WRITES
// fresh state to NVMe).

template <typename W>
concept is_warm_publish_admissible =
    W::template satisfies<CipherTierTag_v::Warm>;

static_assert( is_warm_publish_admissible<HotInt>,
    "Hot-tier value MUST pass the Cipher::publish_warm admission "
    "gate (Hot subsumes Warm — replicating Hot to Warm is a valid "
    "downgrade).");
static_assert( is_warm_publish_admissible<WarmInt>,
    "Warm-tier value MUST pass the Cipher::publish_warm admission "
    "gate (self-admission).");
static_assert(!is_warm_publish_admissible<ColdInt>,
    "Cold-tier value MUST be REJECTED at the Cipher::publish_warm "
    "admission gate — Cold is BELOW Warm in the chain; allowing "
    "publication of a Cold-tier value as Warm would defeat the "
    "freshness guarantee that downstream Augur drift-attribution "
    "depends on.");

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
    if (HotInt::tier != CipherTierTag_v::Hot) {
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
    auto relaxed_copy = source.relax<CipherTierTag_v::Warm>();
    auto relaxed_move = std::move(source).relax<CipherTierTag_v::Cold>();
    [[maybe_unused]] auto rcopy = relaxed_copy.peek();
    [[maybe_unused]] auto rmove = relaxed_move.peek();

    // satisfies<...> at runtime.
    [[maybe_unused]] bool s1 = HotInt::satisfies<CipherTierTag_v::Warm>;
    [[maybe_unused]] bool s2 = WarmInt::satisfies<CipherTierTag_v::Hot>;

    // operator== — same-tier.
    HotInt eq_a{42};
    HotInt eq_b{42};
    if (!(eq_a == eq_b)) std::abort();

    // Move-construct from consumed inner.
    HotInt orig{55};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();

    // Convenience-alias instantiation.
    cipher_tier::Hot<int>  alias_hot{123};
    cipher_tier::Warm<int> alias_warm{456};
    cipher_tier::Cold<int> alias_cold{789};
    [[maybe_unused]] auto av = alias_hot.peek();
    [[maybe_unused]] auto wv = alias_warm.peek();
    [[maybe_unused]] auto cv = alias_cold.peek();

    // Hot-reshard + Warm-publish admission simulations at runtime.
    [[maybe_unused]] bool can_hot_reshard  = is_hot_reshard_admissible<HotInt>;
    [[maybe_unused]] bool can_warm_publish = is_warm_publish_admissible<HotInt>;
}

}  // namespace detail::cipher_tier_self_test

}  // namespace crucible::safety
