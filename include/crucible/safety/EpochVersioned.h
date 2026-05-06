#pragma once

// ── crucible::safety::EpochVersioned<T> ─────────────────────────────
//
// Per-instance fleet-version wrapper.  A value of type T paired with
// TWO independent monotonic counters — Canopy fleet epoch + per-
// Relay generation — composed via the binary product lattice:
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     ProductLattice<EpochLattice, GenerationLattice>,
//                     T>
//   Regime:    4 (per-instance grade with TWO non-empty fields,
//                 16 bytes of grade carried per instance — second
//                 PRODUCT-LATTICE wrapper to ship after Budgeted)
//
// Citation: CRUCIBLE.md §L13 (Canopy Raft-committed membership
// epoch + per-Relay generation); §L14 (Cipher reincarnation across
// topology changes); 28_04_2026_effects.md §4.4.2 (FOUND-G67/G68).
//
// THE LOAD-BEARING USE CASE: every Canopy collective; every reshard
// event; every Cipher hot-tier replication; every Keeper recovery
// path that loads a checkpoint.  An EpochVersioned<T> value carries
// the (epoch, generation) pair of the cluster state at which T was
// produced.  Stale-view detection is done at the admission gate
// via `is_at_least(min_epoch, min_gen)` — values from older epochs
// or older generations are rejected when the fresh-view discipline
// matters.
//
// ── One composition operation, one admission gate ──────────────────
//
// Unlike Budgeted (which has BOTH lattice-join AND saturating-add
// composition), EpochVersioned has ONLY the lattice-join operation
// because epochs and generations are NOT additive — you don't sum
// two epochs together.  The only meaningful composition is
// "fold these two views and keep the more-recent one":
//
//   .combine_max(other)   — pointwise lattice JOIN; "fan-in fold
//                           that keeps the freshest view".  Used
//                           by Canopy collectives gathering peer
//                           state at reshard time.
//
//   .is_at_least(min_epoch, min_gen)
//                         — runtime admission gate; pointwise leq
//                           on both axes.  Used by Keeper recovery
//                           to reject pre-reshard checkpoints.
//
// ── Forward-progress discipline ────────────────────────────────────
//
// The lattice direction (numeric ≤) admits both directions of
// construction; the wrapper does NOT prevent constructing an older
// (epoch, gen) pair after a newer one.  Forward progress is a
// CALL-SITE discipline for ordinary values:
//
//   - Canopy publishes the new fleet epoch ONLY via Raft commit.
//   - Each Relay advances its own generation ONLY at restart.
//
// Production callers should derive the (epoch, gen) pair from those
// authoritative sources, not from arbitrary inputs.  Delegated session
// handoffs now have an additional type-level fence:
// sessions/SessionDelegate.h::EpochedDelegate<T, K, MinEpoch, MinGen>
// carries the required minimum epoch/generation in the protocol type,
// and its peer-side EpochedAccept can only be instantiated under an
// EpochCtx<E, G> satisfying E >= MinEpoch and G >= MinGen.  That
// compile-time fence covers cross-Relay reshard handoffs; per-value
// admission still uses this wrapper's runtime is_at_least gate.
//
//   Axiom coverage:
//     TypeSafe — Epoch and Generation are strong-typed uint64_t
//                newtypes.  Mixing axes (passing a Generation
//                where Epoch is expected, or vice versa) is a
//                compile error — verified by neg-fixtures.
//     DetSafe — every operation is constexpr.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//   Runtime cost:
//     sizeof(EpochVersioned<T>) == sizeof(T) + 16 bytes (two uint64_t
//     grades) + alignment padding.  Verified by static_assert below.
//     REGIME-4, same as Budgeted.
//
// ── No relax<>() — grade is RUNTIME, not type-pinned ───────────────
//
// Same reasoning as Budgeted: the (epoch, gen) pair is RUNTIME data
// derived from the Raft commit log.  Type-pinning epoch / generation
// at the template level would fork the API at every call site.  A
// future EpochVersionedAt<epoch, gen, T> wrapper (if needed) lives
// elsewhere.
//
// See FOUND-G67 (algebra/lattices/{EpochLattice, GenerationLattice}.h)
// for the underlying lattices; FOUND-G68 (this file) for the wrapper;
// safety/Budgeted.h for the sister product wrapper this mirrors;
// CRUCIBLE.md §L13 for the production-call-site rationale.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/EpochLattice.h>
#include <crucible/algebra/lattices/GenerationLattice.h>
#include <crucible/algebra/lattices/ProductLattice.h>

#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the two component types into safety:: under canonical names.
using ::crucible::algebra::lattices::Epoch;
using ::crucible::algebra::lattices::EpochLattice;
using ::crucible::algebra::lattices::Generation;
using ::crucible::algebra::lattices::GenerationLattice;

template <typename T>
class [[nodiscard]] EpochVersioned {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = ::crucible::algebra::lattices::ProductLattice<
        EpochLattice, GenerationLattice>;
    using version_t    = typename lattice_type::element_type;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

private:
    graded_type impl_;

    // Helper: pack the two axes into a version_t product element.
    [[nodiscard]] static constexpr version_t pack(Epoch ep, Generation gen) noexcept {
        return version_t{ep, gen};
    }

public:
    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at (epoch=0, generation=0) — the genesis position.
    constexpr EpochVersioned() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, lattice_type::bottom()} {}

    // Explicit construction from value + both version axes.  The
    // most common production pattern — a Canopy publisher constructs
    // the wrapper at the (committed_epoch, local_generation) pair.
    constexpr EpochVersioned(T value, Epoch ep, Generation gen)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), pack(ep, gen)} {}

    // In-place T construction with explicit version pair.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr EpochVersioned(std::in_place_t, Epoch ep, Generation gen, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pack(ep, gen)} {}

    // Convenience factory: value at the genesis position.  Production
    // boot path — initial Vigil state before any Canopy reshard.
    [[nodiscard]] static constexpr EpochVersioned at_genesis(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return EpochVersioned{std::move(value), Epoch{0}, Generation{0}};
    }

    // Defaulted copy/move/destroy.
    constexpr EpochVersioned(const EpochVersioned&)            = default;
    constexpr EpochVersioned(EpochVersioned&&)                 = default;
    constexpr EpochVersioned& operator=(const EpochVersioned&) = default;
    constexpr EpochVersioned& operator=(EpochVersioned&&)      = default;
    ~EpochVersioned()                                          = default;

    // Equality: compares value bytes AND both version axes.
    [[nodiscard]] friend constexpr bool operator==(
        EpochVersioned const& a, EpochVersioned const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek()
            && a.epoch()      == b.epoch()
            && a.generation() == b.generation();
    }

    // ── Diagnostic names ────────────────────────────────────────────
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

    // ── Per-axis accessors ──────────────────────────────────────────
    [[nodiscard]] constexpr Epoch epoch() const noexcept {
        return impl_.grade().first;
    }

    [[nodiscard]] constexpr Generation generation() const noexcept {
        return impl_.grade().second;
    }

    [[nodiscard]] constexpr version_t version() const noexcept {
        return impl_.grade();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(EpochVersioned& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(EpochVersioned& a, EpochVersioned& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── combine_max — pointwise lattice JOIN ───────────────────────
    //
    // Returns a new EpochVersioned whose (epoch, generation) is the
    // componentwise max of the two inputs' versions.  Used for fan-
    // in admission at Canopy collectives where the cluster's freshest
    // view across two parallel paths must be tracked.
    //
    // VALUE provenance: takes the value from `*this` (the LHS).
    // Same convention as Budgeted::combine_max.
    [[nodiscard]] constexpr EpochVersioned combine_max(EpochVersioned const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return EpochVersioned{
            this->peek(),
            EpochLattice::join(this->epoch(),         other.epoch()),
            GenerationLattice::join(this->generation(), other.generation())
        };
    }

    [[nodiscard]] constexpr EpochVersioned combine_max(EpochVersioned const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        Epoch      joined_epoch =
            EpochLattice::join(this->epoch(),       other.epoch());
        Generation joined_gen   =
            GenerationLattice::join(this->generation(), other.generation());
        return EpochVersioned{std::move(impl_).consume(), joined_epoch, joined_gen};
    }

    // ── is_at_least — runtime admission gate ───────────────────────
    //
    // Returns true iff this EpochVersioned's (epoch, generation) is
    // pointwise ≥ the declared threshold.  Production usage:
    //
    //   if (!checkpoint.is_at_least(committed_epoch, my_generation))
    //       return reject_stale_checkpoint();
    //
    // Both axes are independent — admission requires BOTH to meet
    // the threshold.
    //
    // NOTE: this is the OPPOSITE direction from Budgeted::satisfies
    // (which checks "value's footprint ≤ threshold"), because the
    // semantic is reversed.  Budgeted asks "is the footprint within
    // budget?"; EpochVersioned asks "is the value at least as fresh
    // as required?"  The lattice direction is the same; only the
    // admission-gate phrasing differs.
    [[nodiscard]] constexpr bool is_at_least(Epoch min_epoch,
                                             Generation min_gen) const noexcept
    {
        return EpochLattice::leq(min_epoch, this->epoch())
            && GenerationLattice::leq(min_gen, this->generation());
    }
};

// ── Layout invariants — regime-4 (non-EBO) ──────────────────────────
namespace detail::epoch_versioned_layout {

static_assert(sizeof(EpochVersioned<int>)       >= sizeof(int)    + 16);
static_assert(sizeof(EpochVersioned<double>)    >= sizeof(double) + 16);
static_assert(sizeof(EpochVersioned<char>)      >= sizeof(char)   + 16);

// Strict equality on T=uint64_t: 8(value) + 16(grade) = 24 bytes.
static_assert(sizeof(EpochVersioned<std::uint64_t>) == 24,
    "EpochVersioned<uint64_t>: expected 8(value) + 16(grade) = 24 "
    "bytes.  If this fires, the ProductLattice element_type drifted "
    "from its documented two-uint64_t layout — investigate before "
    "merging.");

}  // namespace detail::epoch_versioned_layout

// ── Cross-axis disjointness — load-bearing for axis-swap fence ────
//
// Both Epoch and Generation wrap uint64_t but are STRUCTURALLY
// DISJOINT C++ types.  This assertion catches a refactor that
// accidentally collapses them into a shared `VersionCounter`
// alias for "convenience" — every neg-fixture's compile error
// would dissolve, and downstream Canopy reshard validation gates
// would silently mix epochs against generations.  Lives at the
// wrapper layer (this file) because that's where both component
// newtypes are guaranteed in scope.
static_assert(!std::is_same_v<Epoch, Generation>,
    "Epoch and Generation must be structurally distinct C++ types "
    "even though both wrap uint64_t.  If this fires, the strong-"
    "newtype discipline that fences EpochVersioned axis-swap bugs "
    "has been broken.");

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::epoch_versioned_self_test {

using EpochVersionedInt = EpochVersioned<int>;
using EpochVersionedDbl = EpochVersioned<double>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr EpochVersionedInt v_default{};
static_assert(v_default.peek()       == 0);
static_assert(v_default.epoch()      == Epoch{0});
static_assert(v_default.generation() == Generation{0});

inline constexpr EpochVersionedInt v_explicit{42, Epoch{5}, Generation{2}};
static_assert(v_explicit.peek()       == 42);
static_assert(v_explicit.epoch()      == Epoch{5});
static_assert(v_explicit.generation() == Generation{2});

inline constexpr EpochVersionedInt v_in_place{
    std::in_place, Epoch{3}, Generation{1}, 7};
static_assert(v_in_place.peek()       == 7);
static_assert(v_in_place.epoch()      == Epoch{3});
static_assert(v_in_place.generation() == Generation{1});

// ── Convenience factories ─────────────────────────────────────────
inline constexpr EpochVersionedInt v_genesis = EpochVersionedInt::at_genesis(99);
static_assert(v_genesis.peek()       == 99);
static_assert(v_genesis.epoch()      == Epoch{0});
static_assert(v_genesis.generation() == Generation{0});

// ── combine_max — lattice join semantics ──────────────────────────
//
// Fan-in fold across two cluster views: keeps the freshest view.
[[nodiscard]] consteval bool combine_max_takes_pointwise_max() noexcept {
    EpochVersionedInt a{42, Epoch{5}, Generation{1}};
    EpochVersionedInt b{42, Epoch{3}, Generation{4}};
    auto              c = a.combine_max(b);
    return c.epoch()      == Epoch{5}                // max(5, 3)
        && c.generation() == Generation{4}           // max(1, 4)
        && c.peek()       == 42;
}
static_assert(combine_max_takes_pointwise_max());

// Reflexivity: combining with self is identity (idempotent join).
[[nodiscard]] consteval bool combine_max_idempotent() noexcept {
    EpochVersionedInt a{42, Epoch{5}, Generation{2}};
    auto              c = a.combine_max(a);
    return c.epoch() == Epoch{5} && c.generation() == Generation{2};
}
static_assert(combine_max_idempotent());

// ── is_at_least — admission gate semantics ───────────────────────
[[nodiscard]] consteval bool is_at_least_passes_within_threshold() noexcept {
    EpochVersionedInt v{42, Epoch{5}, Generation{2}};
    return  v.is_at_least(Epoch{5}, Generation{2})    // exact match (boundary)
        &&  v.is_at_least(Epoch{4}, Generation{1})    // both lower
        && !v.is_at_least(Epoch{6}, Generation{2})    // epoch over
        && !v.is_at_least(Epoch{5}, Generation{3});   // generation over
}
static_assert(is_at_least_passes_within_threshold());

// Genesis fails any non-genesis threshold (per direction convention).
static_assert(!EpochVersionedInt::at_genesis(7).is_at_least(
    Epoch{1}, Generation{0}));

// At-top passes any threshold below the cap.
static_assert(EpochVersionedInt{
    7, EpochLattice::top(), GenerationLattice::top()}.is_at_least(
    Epoch{1u<<30}, Generation{1u<<30}));

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(EpochVersionedInt::value_type_name().ends_with("int"));
static_assert(EpochVersionedInt::lattice_name().size() > 0);

// ── swap exchanges T values within the same lattice pin ──────────
template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x, Epoch{1}, Generation{2}};
    W b{y, Epoch{3}, Generation{4}};
    a.swap(b);
    return a.peek()       == y
        && b.peek()       == x
        && a.epoch()      == Epoch{3}
        && b.generation() == Generation{2};
}
static_assert(swap_exchanges_within<EpochVersionedInt>(10, 20));

[[nodiscard]] consteval bool free_swap_works() noexcept {
    EpochVersionedInt a{10, Epoch{1}, Generation{2}};
    EpochVersionedInt b{20, Epoch{3}, Generation{4}};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10
        && a.epoch() == Epoch{3} && b.generation() == Generation{2};
}
static_assert(free_swap_works());

// ── peek_mut allows in-place T mutation ──────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    EpochVersionedInt a{10, Epoch{5}, Generation{2}};
    a.peek_mut() = 99;
    return a.peek() == 99 && a.epoch() == Epoch{5};
}
static_assert(peek_mut_works());

// ── operator== — same-lattice, same-T comparison ─────────────────
[[nodiscard]] consteval bool equality_compares_value_and_version() noexcept {
    EpochVersionedInt a{42, Epoch{5}, Generation{2}};
    EpochVersionedInt b{42, Epoch{5}, Generation{2}};
    EpochVersionedInt c{43, Epoch{5}, Generation{2}};   // diff value
    EpochVersionedInt d{42, Epoch{6}, Generation{2}};   // diff epoch
    EpochVersionedInt e{42, Epoch{5}, Generation{3}};   // diff generation
    return  (a == b)
        && !(a == c)
        && !(a == d)
        && !(a == e);
}
static_assert(equality_compares_value_and_version());

// ── Move-only T support ──────────────────────────────────────────
struct MoveOnlyT {
    int v{0};
    constexpr MoveOnlyT() = default;
    constexpr explicit MoveOnlyT(int x) : v{x} {}
    constexpr MoveOnlyT(MoveOnlyT&&) = default;
    constexpr MoveOnlyT& operator=(MoveOnlyT&&) = default;
    MoveOnlyT(MoveOnlyT const&) = delete;
    MoveOnlyT& operator=(MoveOnlyT const&) = delete;
};

static_assert(!std::is_copy_constructible_v<EpochVersioned<MoveOnlyT>>,
    "EpochVersioned<T> must transitively inherit T's copy-deletion.");
static_assert(std::is_move_constructible_v<EpochVersioned<MoveOnlyT>>);

// combine_max && rvalue overload for move-only T.
[[nodiscard]] consteval bool combine_max_works_for_move_only() noexcept {
    EpochVersioned<MoveOnlyT> a{MoveOnlyT{42}, Epoch{1}, Generation{1}};
    EpochVersioned<MoveOnlyT> b{MoveOnlyT{99}, Epoch{5}, Generation{0}};
    auto                      c = std::move(a).combine_max(b);
    return c.epoch()      == Epoch{5}
        && c.generation() == Generation{1}
        && c.peek().v     == 42;        // value from `a`
}
static_assert(combine_max_works_for_move_only());

// SFINAE detectors — concept-level rejection on copy-only paths.
template <typename W>
concept can_combine_max_lvalue = requires(W const& a, W const& b) {
    { a.combine_max(b) };
};
template <typename W>
concept can_combine_max_rvalue = requires(W&& a, W const& b) {
    { std::move(a).combine_max(b) };
};
static_assert( can_combine_max_lvalue<EpochVersionedInt>);
static_assert( can_combine_max_rvalue<EpochVersionedInt>);
static_assert(!can_combine_max_lvalue<EpochVersioned<MoveOnlyT>>,
    "combine_max const& on move-only T must be rejected.");
static_assert( can_combine_max_rvalue<EpochVersioned<MoveOnlyT>>);

// ── Stable-name introspection ────────────────────────────────────
static_assert(EpochVersionedInt::value_type_name().size() > 0);
static_assert(EpochVersionedInt::lattice_name().size()    > 0);

// ── Runtime smoke test ────────────────────────────────────────────
inline void runtime_smoke_test() {
    EpochVersionedInt a{};
    EpochVersionedInt b{42, Epoch{5}, Generation{2}};
    EpochVersionedInt c{std::in_place, Epoch{3}, Generation{1}, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();
    [[maybe_unused]] auto eb = b.epoch();
    [[maybe_unused]] auto gb = b.generation();

    // Genesis factory.
    EpochVersionedInt d = EpochVersionedInt::at_genesis(99);
    if (d.epoch() != Epoch{0}) std::abort();

    // peek_mut.
    EpochVersionedInt mutable_b{10, Epoch{1}, Generation{1}};
    mutable_b.peek_mut() = 99;
    if (mutable_b.peek() != 99) std::abort();

    // Swap.
    EpochVersionedInt sx{1, Epoch{1}, Generation{1}};
    EpochVersionedInt sy{2, Epoch{2}, Generation{2}};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // combine_max.
    EpochVersionedInt left {42, Epoch{5}, Generation{1}};
    EpochVersionedInt right{42, Epoch{3}, Generation{4}};
    auto              joined = left.combine_max(right);
    if (joined.epoch() != Epoch{5}) std::abort();
    if (joined.generation() != Generation{4}) std::abort();

    // is_at_least admission gate.
    EpochVersionedInt observed{42, Epoch{5}, Generation{2}};
    if (!observed.is_at_least(Epoch{4}, Generation{1})) std::abort();
    if ( observed.is_at_least(Epoch{6}, Generation{0})) std::abort();

    // operator==.
    EpochVersionedInt eq_a{42, Epoch{1}, Generation{1}};
    EpochVersionedInt eq_b{42, Epoch{1}, Generation{1}};
    if (!(eq_a == eq_b)) std::abort();

    // version() returns ProductElement.
    [[maybe_unused]] auto version_pair = b.version();
    if (version_pair.first  != Epoch{5})       std::abort();
    if (version_pair.second != Generation{2})  std::abort();

    // Move-construct from consumed inner.
    EpochVersionedInt orig{55, Epoch{1}, Generation{1}};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();
}

}  // namespace detail::epoch_versioned_self_test

}  // namespace crucible::safety
