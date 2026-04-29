#pragma once

// ── crucible::safety::NumaPlacement<T> ──────────────────────────────
//
// Per-instance NUMA-placement wrapper.  A value of type T paired
// with TWO independent placement-discipline axes — NUMA node
// identifier (partial-order with wildcard top) + CPU affinity
// bitmask (boolean lattice) — composed via the binary product
// lattice:
//
//   Substrate: Graded<ModalityKind::Absolute,
//                     ProductLattice<NumaNodeLattice, AffinityLattice>,
//                     T>
//   Regime:    4 (per-instance grade with TWO non-empty fields,
//                 9 bytes of grade carried per instance — the THIRD
//                 PRODUCT-LATTICE wrapper to ship after Budgeted /
//                 EpochVersioned, and the FIRST product wrapper
//                 mixing TWO DIFFERENT algebraic shapes (partial-
//                 order + boolean lattice) at the component layer)
//
// Citation: THREADING.md §5.4 (cache-tier rule with NUMA-local
// placement); CRUCIBLE.md §L13 (NUMA-aware ThreadPool worker
// affinity); 28_04_2026_effects.md §4.4.3 (FOUND-G71/G72).
//
// THE LOAD-BEARING USE CASE: AdaptiveScheduler placement decisions
// + NumaThreadPool worker assignment.  A NumaPlacement<T> value
// carries the (node, affinity) pair declaring where T can be
// scheduled.  At dispatch time the scheduler asks "can this task
// run at (node=N, core=C)?" via `admits(N, single_core_mask(C))`
// — admission iff value's claim subsumes the schedule slot's
// specifics.
//
// ── Algebraic asymmetry — partial-order × boolean lattice ──────────
//
// Unlike Budgeted (chain × chain) and EpochVersioned (chain × chain),
// NumaPlacement composes a PARTIAL-ORDER lattice (NumaNodeLattice
// — siblings are incomparable) with a BOOLEAN lattice (AffinityLattice
// — every element has a complement).  The product lattice still
// satisfies Lattice / BoundedLattice axioms: join and meet are
// pointwise, distributivity holds because each component is
// distributive (chain lattices and boolean lattices are both
// distributive — partial-orders aren't always, but NumaNodeLattice's
// specific shape with sibling = ⊤/⊥-resolution IS distributive at
// every triple).
//
// Verified at compile time via the SelfTest block below.
//
// ── One composition operation, one admission gate ──────────────────
//
//   .combine_max(other)   — pointwise lattice JOIN.  For NumaNode:
//                           returns `Any` if siblings, else the
//                           more-specific.  For Affinity: union of
//                           cores.  Used for fan-in placement
//                           decisions where two upstream paths'
//                           constraints must both be satisfied at
//                           the consumer.
//
//   .admits(req_node, req_affinity)
//                         — runtime admission gate.  True iff this
//                           value's placement claim SUBSUMES the
//                           request:
//                             leq(req_node,     this.numa_node())
//                             leq(req_affinity, this.affinity())
//                           where leq is each component's leq.
//                           Production callers query at every
//                           dispatch site.
//
// ── Forward-progress / rebinding discipline (NOT type-enforced) ────
//
// Same as Budgeted and EpochVersioned: the lattice direction admits
// arbitrary construction; the wrapper does NOT prevent rebinding to
// an incompatible (node, affinity).  Production callers should
// derive the placement from authoritative sources (the AdaptiveScheduler's
// TopologyMatrix + NumaThreadPool's worker map), not from arbitrary
// inputs.
//
//   Axiom coverage:
//     TypeSafe — NumaNodeId is a strong scoped enum, AffinityMask
//                is a strong-typed uint64_t newtype.  Mixing the
//                axes (passing AffinityMask where NumaNodeId is
//                expected, or vice versa) is a compile error.
//     DetSafe — every operation is constexpr.
//     MemSafe — defaulted copy/move; T's move semantics carry through.
//   Runtime cost:
//     sizeof(NumaPlacement<T>) >= sizeof(T) + 9 bytes (1 byte
//     NumaNodeId + 8 bytes AffinityMask) + alignment padding.
//     REGIME-4, same as Budgeted/EpochVersioned.
//
// See FOUND-G71 (algebra/lattices/{NumaNodeLattice, AffinityLattice}.h)
// for the underlying lattices; FOUND-G72 (this file) for the wrapper;
// safety/Budgeted.h and safety/EpochVersioned.h for the sister
// product wrappers; THREADING.md §5.4 for the production rationale.

#include <crucible/Platform.h>
#include <crucible/algebra/Graded.h>
#include <crucible/algebra/lattices/AffinityLattice.h>
#include <crucible/algebra/lattices/NumaNodeLattice.h>
#include <crucible/algebra/lattices/ProductLattice.h>

#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::safety {

// Hoist the two component types into safety:: under canonical names.
using ::crucible::algebra::lattices::AffinityLattice;
using ::crucible::algebra::lattices::AffinityMask;
using ::crucible::algebra::lattices::NumaNodeId;
using ::crucible::algebra::lattices::NumaNodeLattice;

// ── Cross-axis disjointness — load-bearing for axis-swap fence ────
//
// NumaNodeId (a 1-byte strong scoped enum) and AffinityMask (an
// 8-byte strong-typed uint64_t newtype) are STRUCTURALLY DISTINCT
// C++ types, so this assertion is mostly defensive — but ships
// alongside the sister product wrappers' identical fences for
// pattern uniformity and to catch any future refactor that drifts.
static_assert(!std::is_same_v<NumaNodeId, AffinityMask>,
    "NumaNodeId and AffinityMask must be structurally distinct "
    "C++ types.  If this fires, the strong-newtype discipline "
    "that fences NumaPlacement axis-swap bugs has been broken.");

template <typename T>
class [[nodiscard]] NumaPlacement {
public:
    // ── Public type aliases ─────────────────────────────────────────
    using value_type   = T;
    using lattice_type = ::crucible::algebra::lattices::ProductLattice<
        NumaNodeLattice, AffinityLattice>;
    using placement_t  = typename lattice_type::element_type;
    using graded_type  = ::crucible::algebra::Graded<
        ::crucible::algebra::ModalityKind::Absolute,
        lattice_type,
        T>;
    static constexpr ::crucible::algebra::ModalityKind modality =
        ::crucible::algebra::ModalityKind::Absolute;

private:
    graded_type impl_;

    [[nodiscard]] static constexpr placement_t pack(NumaNodeId node, AffinityMask aff) noexcept {
        return placement_t{node, aff};
    }

public:
    // ── Construction ────────────────────────────────────────────────
    //
    // Default: T{} at (node=None, affinity=empty) — the most
    // restrictive position (admits nowhere).
    constexpr NumaPlacement() noexcept(std::is_nothrow_default_constructible_v<T>)
        : impl_{T{}, lattice_type::bottom()} {}

    // Explicit construction from value + both placement axes.
    constexpr NumaPlacement(T value, NumaNodeId node, AffinityMask aff)
        noexcept(std::is_nothrow_move_constructible_v<T>)
        : impl_{std::move(value), pack(node, aff)} {}

    // In-place T construction with explicit placement pair.
    template <typename... Args>
        requires std::is_constructible_v<T, Args...>
    constexpr NumaPlacement(std::in_place_t, NumaNodeId node, AffinityMask aff, Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>
                 && std::is_nothrow_move_constructible_v<T>)
        : impl_{T(std::forward<Args>(args)...), pack(node, aff)} {}

    // Convenience factory: NUMA-agnostic, schedulable on any core.
    // The wildcard placement — used for NUMA-agnostic data
    // (constants, configuration, the Cipher cold-tier).
    [[nodiscard]] static constexpr NumaPlacement anywhere(T value)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return NumaPlacement{std::move(value), NumaNodeId::Any,
                             AffinityLattice::top()};
    }

    // Convenience factory: pin to a specific (node, single-core)
    // placement.  Production NumaThreadPool worker self-binding.
    [[nodiscard]] static constexpr NumaPlacement pinned(T value,
                                                        NumaNodeId node,
                                                        std::uint8_t core)
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        return NumaPlacement{std::move(value), node,
                             AffinityMask::single(core)};
    }

    // Defaulted copy/move/destroy.
    constexpr NumaPlacement(const NumaPlacement&)            = default;
    constexpr NumaPlacement(NumaPlacement&&)                 = default;
    constexpr NumaPlacement& operator=(const NumaPlacement&) = default;
    constexpr NumaPlacement& operator=(NumaPlacement&&)      = default;
    ~NumaPlacement()                                         = default;

    // Equality: compares value bytes AND both placement axes.
    [[nodiscard]] friend constexpr bool operator==(
        NumaPlacement const& a, NumaPlacement const& b) noexcept(
        noexcept(a.peek() == b.peek()))
        requires requires(T const& x, T const& y) { { x == y } -> std::convertible_to<bool>; }
    {
        return a.peek() == b.peek()
            && a.numa_node() == b.numa_node()
            && a.affinity()  == b.affinity();
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
    [[nodiscard]] constexpr NumaNodeId numa_node() const noexcept {
        return impl_.grade().first;
    }

    [[nodiscard]] constexpr AffinityMask affinity() const noexcept {
        return impl_.grade().second;
    }

    [[nodiscard]] constexpr placement_t placement() const noexcept {
        return impl_.grade();
    }

    // ── swap ────────────────────────────────────────────────────────
    constexpr void swap(NumaPlacement& other)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        impl_.swap(other.impl_);
    }

    friend constexpr void swap(NumaPlacement& a, NumaPlacement& b)
        noexcept(std::is_nothrow_swappable_v<T>)
    {
        a.swap(b);
    }

    // ── combine_max — pointwise lattice JOIN ───────────────────────
    [[nodiscard]] constexpr NumaPlacement combine_max(NumaPlacement const& other) const&
        noexcept(std::is_nothrow_copy_constructible_v<T>)
        requires std::copy_constructible<T>
    {
        return NumaPlacement{
            this->peek(),
            NumaNodeLattice::join(this->numa_node(), other.numa_node()),
            AffinityLattice::join(this->affinity(),  other.affinity())
        };
    }

    [[nodiscard]] constexpr NumaPlacement combine_max(NumaPlacement const& other) &&
        noexcept(std::is_nothrow_move_constructible_v<T>)
    {
        NumaNodeId   joined_node = NumaNodeLattice::join(this->numa_node(), other.numa_node());
        AffinityMask joined_aff  = AffinityLattice::join(this->affinity(),  other.affinity());
        return NumaPlacement{std::move(impl_).consume(), joined_node, joined_aff};
    }

    // ── admits — runtime admission gate ────────────────────────────
    //
    // Returns true iff this placement claim SUBSUMES the requested
    // (node, affinity) — i.e., the scheduler's request fits inside
    // the value's placement constraints.  The lattice-direction
    // reading: leq(request, claim) on each axis pointwise.
    //
    // Production usage:
    //
    //   if (!task.admits(target_node, AffinityMask::single(core)))
    //       return reject_placement_violation();
    [[nodiscard]] constexpr bool admits(NumaNodeId   req_node,
                                        AffinityMask req_affinity) const noexcept
    {
        return NumaNodeLattice::leq(req_node,     this->numa_node())
            && AffinityLattice::leq(req_affinity, this->affinity());
    }
};

// ── Layout invariants — regime-4 (non-EBO) ──────────────────────────
//
// NumaPlacement<T> carries a placement_t = (NumaNodeId, AffinityMask)
// = (1 byte, kWords*8 = 32 bytes for kWords=4) = 33 bytes of grade
// nominally, but C++ aggregate alignment on the AffinityMask
// (8-byte-aligned array<uint64_t, kWords>) forces the placement_t
// to 40 bytes (1 byte + 7 padding + 32 bytes).
//
// Total layout for T=uint64_t (8-byte aligned):
//   value (8) + grade (40) = 48 bytes flat.
namespace detail::numa_placement_layout {

constexpr std::size_t kAffinityBytes = AffinityMask::kWords * sizeof(std::uint64_t);
constexpr std::size_t kPlacementBytes = 8 + kAffinityBytes;  // 1 byte + 7 pad + N words

static_assert(sizeof(NumaPlacement<int>)       >= sizeof(int)    + kAffinityBytes + 1);
static_assert(sizeof(NumaPlacement<double>)    >= sizeof(double) + kAffinityBytes + 1);
static_assert(sizeof(NumaPlacement<char>)      >= sizeof(char)   + kAffinityBytes + 1);

// For T=uint64_t with 8-byte alignment:
//   value (8) + grade (1+7pad+kAffinityBytes) = 8 + 8 + kAffinityBytes
static_assert(sizeof(NumaPlacement<std::uint64_t>) == 8 + kPlacementBytes,
    "NumaPlacement<uint64_t>: layout drifted from value(8) + "
    "aligned-grade(8 + AffinityMask::kWords*8) — investigate.  "
    "Bumping AffinityMask::kWords requires updating this assertion.");

}  // namespace detail::numa_placement_layout

// ── Self-test ───────────────────────────────────────────────────────
namespace detail::numa_placement_self_test {

using NumaPlacementInt = NumaPlacement<int>;
using NumaPlacementDbl = NumaPlacement<double>;

// ── Construction paths ─────────────────────────────────────────────
inline constexpr NumaPlacementInt p_default{};
static_assert(p_default.peek()       == 0);
static_assert(p_default.numa_node()  == NumaNodeId::None);
static_assert(p_default.affinity()   == AffinityMask{0});

inline constexpr NumaPlacementInt p_explicit{
    42, NumaNodeId{2}, AffinityMask{0b1100}};
static_assert(p_explicit.peek()       == 42);
static_assert(p_explicit.numa_node()  == NumaNodeId{2});
static_assert(p_explicit.affinity()   == AffinityMask{0b1100});

inline constexpr NumaPlacementInt p_in_place{
    std::in_place, NumaNodeId{1}, AffinityMask{0b11}, 7};
static_assert(p_in_place.peek()      == 7);
static_assert(p_in_place.numa_node() == NumaNodeId{1});

// ── Convenience factories ─────────────────────────────────────────
inline constexpr NumaPlacementInt p_anywhere = NumaPlacementInt::anywhere(99);
static_assert(p_anywhere.peek()       == 99);
static_assert(p_anywhere.numa_node()  == NumaNodeId::Any);
static_assert(p_anywhere.affinity()   == AffinityLattice::top());

inline constexpr NumaPlacementInt p_pinned =
    NumaPlacementInt::pinned(11, NumaNodeId{2}, /*core=*/3);
static_assert(p_pinned.peek()       == 11);
static_assert(p_pinned.numa_node()  == NumaNodeId{2});
static_assert(p_pinned.affinity()   == AffinityMask::single(3));

// ── combine_max — lattice join semantics ──────────────────────────
//
// SAME-NODE join: nodes match → preserved; affinities union.
[[nodiscard]] consteval bool combine_max_same_node() noexcept {
    NumaPlacementInt a{42, NumaNodeId{2}, AffinityMask{0b001}};
    NumaPlacementInt b{42, NumaNodeId{2}, AffinityMask{0b010}};
    auto             c = a.combine_max(b);
    return c.numa_node() == NumaNodeId{2}
        && c.affinity()  == AffinityMask{0b011}
        && c.peek()      == 42;
}
static_assert(combine_max_same_node());

// SIBLING-NODE join: nodes incomparable → bumped to Any wildcard.
[[nodiscard]] consteval bool combine_max_sibling_nodes() noexcept {
    NumaPlacementInt a{42, NumaNodeId{0}, AffinityMask{0b001}};
    NumaPlacementInt b{42, NumaNodeId{1}, AffinityMask{0b010}};
    auto             c = a.combine_max(b);
    return c.numa_node() == NumaNodeId::Any        // siblings → Any
        && c.affinity()  == AffinityMask{0b011};   // affinities still union
}
static_assert(combine_max_sibling_nodes());

// Idempotent join with self.
[[nodiscard]] consteval bool combine_max_idempotent() noexcept {
    NumaPlacementInt a{42, NumaNodeId{2}, AffinityMask{0b1100}};
    auto             c = a.combine_max(a);
    return c.numa_node() == NumaNodeId{2}
        && c.affinity()  == AffinityMask{0b1100};
}
static_assert(combine_max_idempotent());

// ── admits — admission gate semantics ─────────────────────────────
[[nodiscard]] consteval bool admits_within_threshold() noexcept {
    NumaPlacementInt v{42, NumaNodeId{2}, AffinityMask{0b1100}};
    return  v.admits(NumaNodeId{2},      AffinityMask{0b0100})    // both within
        &&  v.admits(NumaNodeId::None,   AffinityMask{0b1100})    // node bottom
        && !v.admits(NumaNodeId{3},      AffinityMask{0b0100})    // wrong node
        && !v.admits(NumaNodeId{2},      AffinityMask{0b0010});   // affinity outside
}
static_assert(admits_within_threshold());

// Anywhere placement admits any (specific_node, single_core) request.
static_assert(NumaPlacementInt::anywhere(7).admits(
    NumaNodeId{42}, AffinityMask::single(7)));

// Default (None, empty) admits only (None, empty).
static_assert(NumaPlacementInt{}.admits(NumaNodeId::None, AffinityMask{0}));
static_assert(!NumaPlacementInt{}.admits(NumaNodeId{0},   AffinityMask::single(0)));

// ── Diagnostic forwarders ─────────────────────────────────────────
static_assert(NumaPlacementInt::value_type_name().ends_with("int"));
static_assert(NumaPlacementInt::lattice_name().size() > 0);

// ── swap exchanges T values within the same lattice pin ──────────
template <typename W>
[[nodiscard]] consteval bool swap_exchanges_within(int x, int y) noexcept {
    W a{x, NumaNodeId{0}, AffinityMask{0b01}};
    W b{y, NumaNodeId{1}, AffinityMask{0b10}};
    a.swap(b);
    return a.peek()      == y
        && b.peek()      == x
        && a.numa_node() == NumaNodeId{1}
        && b.affinity()  == AffinityMask{0b01};
}
static_assert(swap_exchanges_within<NumaPlacementInt>(10, 20));

[[nodiscard]] consteval bool free_swap_works() noexcept {
    NumaPlacementInt a{10, NumaNodeId{0}, AffinityMask{0b01}};
    NumaPlacementInt b{20, NumaNodeId{1}, AffinityMask{0b10}};
    using std::swap;
    swap(a, b);
    return a.peek() == 20 && b.peek() == 10
        && a.numa_node() == NumaNodeId{1}
        && b.affinity()  == AffinityMask{0b01};
}
static_assert(free_swap_works());

// ── peek_mut allows in-place T mutation ──────────────────────────
[[nodiscard]] consteval bool peek_mut_works() noexcept {
    NumaPlacementInt a{10, NumaNodeId{2}, AffinityMask{0b1100}};
    a.peek_mut() = 99;
    return a.peek() == 99 && a.numa_node() == NumaNodeId{2};
}
static_assert(peek_mut_works());

// ── operator== — same-lattice, same-T comparison ─────────────────
[[nodiscard]] consteval bool equality_compares_value_and_placement() noexcept {
    NumaPlacementInt a{42, NumaNodeId{2}, AffinityMask{0b11}};
    NumaPlacementInt b{42, NumaNodeId{2}, AffinityMask{0b11}};
    NumaPlacementInt c{43, NumaNodeId{2}, AffinityMask{0b11}};   // diff value
    NumaPlacementInt d{42, NumaNodeId{3}, AffinityMask{0b11}};   // diff node
    NumaPlacementInt e{42, NumaNodeId{2}, AffinityMask{0b10}};   // diff affinity
    return  (a == b)
        && !(a == c)
        && !(a == d)
        && !(a == e);
}
static_assert(equality_compares_value_and_placement());

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

static_assert(!std::is_copy_constructible_v<NumaPlacement<MoveOnlyT>>);
static_assert(std::is_move_constructible_v<NumaPlacement<MoveOnlyT>>);

// combine_max && rvalue overload for move-only T.
[[nodiscard]] consteval bool combine_max_works_for_move_only() noexcept {
    NumaPlacement<MoveOnlyT> a{MoveOnlyT{42}, NumaNodeId{2}, AffinityMask{0b01}};
    NumaPlacement<MoveOnlyT> b{MoveOnlyT{99}, NumaNodeId{2}, AffinityMask{0b10}};
    auto                     c = std::move(a).combine_max(b);
    return c.numa_node() == NumaNodeId{2}
        && c.affinity()  == AffinityMask{0b11}
        && c.peek().v    == 42;
}
static_assert(combine_max_works_for_move_only());

// SFINAE detectors.
template <typename W>
concept can_combine_max_lvalue = requires(W const& a, W const& b) {
    { a.combine_max(b) };
};
template <typename W>
concept can_combine_max_rvalue = requires(W&& a, W const& b) {
    { std::move(a).combine_max(b) };
};
static_assert( can_combine_max_lvalue<NumaPlacementInt>);
static_assert( can_combine_max_rvalue<NumaPlacementInt>);
static_assert(!can_combine_max_lvalue<NumaPlacement<MoveOnlyT>>);
static_assert( can_combine_max_rvalue<NumaPlacement<MoveOnlyT>>);

// ── Stable-name introspection ────────────────────────────────────
static_assert(NumaPlacementInt::value_type_name().size() > 0);
static_assert(NumaPlacementInt::lattice_name().size()    > 0);

// ── Runtime smoke test ────────────────────────────────────────────
inline void runtime_smoke_test() {
    NumaPlacementInt a{};
    NumaPlacementInt b{42, NumaNodeId{2}, AffinityMask{0b1100}};
    NumaPlacementInt c{std::in_place, NumaNodeId{1}, AffinityMask{0b11}, 7};

    [[maybe_unused]] auto va = a.peek();
    [[maybe_unused]] auto vb = b.peek();
    [[maybe_unused]] auto vc = c.peek();
    [[maybe_unused]] auto nb = b.numa_node();
    [[maybe_unused]] auto ab = b.affinity();

    NumaPlacementInt anyw = NumaPlacementInt::anywhere(99);
    if (anyw.numa_node() != NumaNodeId::Any) std::abort();

    NumaPlacementInt pin = NumaPlacementInt::pinned(11, NumaNodeId{3}, 5);
    if (pin.numa_node() != NumaNodeId{3})       std::abort();
    if (pin.affinity()  != AffinityMask::single(5)) std::abort();

    NumaPlacementInt mutable_b{10, NumaNodeId{0}, AffinityMask{0b1}};
    mutable_b.peek_mut() = 99;
    if (mutable_b.peek() != 99) std::abort();

    NumaPlacementInt sx{1, NumaNodeId{0}, AffinityMask{0b01}};
    NumaPlacementInt sy{2, NumaNodeId{1}, AffinityMask{0b10}};
    sx.swap(sy);
    using std::swap;
    swap(sx, sy);

    // combine_max with sibling nodes promotes to Any.
    NumaPlacementInt left {42, NumaNodeId{0}, AffinityMask{0b01}};
    NumaPlacementInt right{42, NumaNodeId{1}, AffinityMask{0b10}};
    auto             joined = left.combine_max(right);
    if (joined.numa_node() != NumaNodeId::Any) std::abort();
    if (joined.affinity()  != AffinityMask{0b11}) std::abort();

    // admits.
    NumaPlacementInt task{42, NumaNodeId{2}, AffinityMask{0b1100}};
    if (!task.admits(NumaNodeId{2}, AffinityMask::single(2))) std::abort();
    if ( task.admits(NumaNodeId{3}, AffinityMask::single(2))) std::abort();

    // operator==.
    NumaPlacementInt eq_a{42, NumaNodeId{0}, AffinityMask{0b1}};
    NumaPlacementInt eq_b{42, NumaNodeId{0}, AffinityMask{0b1}};
    if (!(eq_a == eq_b)) std::abort();

    // placement() returns ProductElement.
    [[maybe_unused]] auto pair = b.placement();
    if (pair.first  != NumaNodeId{2})        std::abort();
    if (pair.second != AffinityMask{0b1100}) std::abort();

    NumaPlacementInt orig{55, NumaNodeId{0}, AffinityMask{0b1}};
    int extracted = std::move(orig).consume();
    if (extracted != 55) std::abort();
}

}  // namespace detail::numa_placement_self_test

}  // namespace crucible::safety
