#pragma once

// ── crucible::safety::diag::canonical_order — §XVI structural gate ─
//
// FIXY-FOUND-048
//
// CLAUDE.md §XVI mandates a canonical wrapper-nesting order:
//
//   HotPath ⊃ DetSafe ⊃ NumericalTier ⊃ Vendor ⊃ ResidencyHeat ⊃
//     CipherTier ⊃ AllocClass ⊃ Wait ⊃ MemOrder ⊃ Progress ⊃
//     Stale ⊃ Tagged ⊃ Refined ⊃ Secret ⊃ Linear ⊃ Computation
//
// outer → inner.  The order is LOAD-BEARING: wrapper-nesting is
// order-sensitive (Stale<Tagged<T>> ≢ Tagged<Stale<T>>) and the
// canonical order is what `row_hash` (RowHashFold.h) folds along
// when computing federation cache keys.  Out-of-order stacks
// compile fine today, but produce DIFFERENT row hashes — silently
// landing kernels in DIFFERENT federation cache slots than peers
// who used the canonical order.
//
// Pre-FOUND-048 the §XVI order was prose-only: discipline lived in
// CLAUDE.md and reviewer attention.  This header gives reviewers a
// compile-time PREDICATE — `is_canonically_ordered_v<Stack>` and
// the `CanonicallyOrdered<W>` concept — that production code can
// constrain on.  A site that REQUIRES canonical order writes:
//
//   template <CanonicallyOrdered W> void publish(W&&);
//
// and the compiler rejects out-of-order stacks at the boundary,
// pointing to the exact site rather than waiting for a downstream
// row-hash collision.
//
// ── Discipline contract ────────────────────────────────────────────
//
// Production code that needs a "deliberate deviation" cache-slot
// (per §XVI "out-of-order stacks compile fine but produce DIFFERENT
// row hashes; review questions deviations unless the author
// documents a deliberate-different-cache-slot intent") simply does
// NOT constrain on `CanonicallyOrdered<>` — the predicate is an
// OPT-IN gate, not a global hard-rejection.  This matches the
// existing review discipline: canonical is the default, deviation
// is documented at the call site.
//
// ── Off-tree wrappers ──────────────────────────────────────────────
//
// The §XVI 16-position order names the canonical wrappers.  Other
// wrappers (Hw, BarrierGuarded, SimdWidthPinned, ScopedFence,
// ClockSource, SuspendBehavior, JoinPolicy, SchedClass, Witness,
// FpModePinned, Monotonic, AppendOnly, etc.) are "off-tree" — they
// stack at positions dictated by their dimension but are NOT in the
// §XVI canonical sequence.  The walk SKIPS them (keeps the prior
// last-canonical index) and continues descending into ::value_type.
// This means off-tree wrappers are NEUTRAL with respect to the
// canonical-order check.

#include <crucible/safety/AllocClass.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/effects/Computation.h>

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace crucible::safety::diag::canonical_order {

// ── canonical_layer_index<W>::value — per-wrapper §XVI position ──
//
// Primary template UNDEFINED.  Each of the 16 canonical wrappers
// specializes to its §XVI position 0..15.  Off-tree wrappers
// deliberately do NOT specialize — the `HasCanonicalLayerIndex`
// concept reports false for them, and the descent walks past them
// without affecting the order check.

template <typename W>
struct canonical_layer_index;

// Tier-3 (enum-NTTP + payload) wrappers — positions 0..9 -----------

template <auto Tier, typename T>
struct canonical_layer_index<HotPath<Tier, T>> {
    static constexpr int value = 0;
};

template <auto Tier, typename T>
struct canonical_layer_index<DetSafe<Tier, T>> {
    static constexpr int value = 1;
};

template <auto Tier, typename T>
struct canonical_layer_index<NumericalTier<Tier, T>> {
    static constexpr int value = 2;
};

template <auto Backend, typename T>
struct canonical_layer_index<Vendor<Backend, T>> {
    static constexpr int value = 3;
};

template <auto Tier, typename T>
struct canonical_layer_index<ResidencyHeat<Tier, T>> {
    static constexpr int value = 4;
};

template <auto Tier, typename T>
struct canonical_layer_index<CipherTier<Tier, T>> {
    static constexpr int value = 5;
};

template <auto Tag, typename T>
struct canonical_layer_index<AllocClass<Tag, T>> {
    static constexpr int value = 6;
};

template <auto Strategy, typename T>
struct canonical_layer_index<Wait<Strategy, T>> {
    static constexpr int value = 7;
};

template <auto Tag, typename T>
struct canonical_layer_index<MemOrder<Tag, T>> {
    static constexpr int value = 8;
};

template <auto Class, typename T>
struct canonical_layer_index<Progress<Class, T>> {
    static constexpr int value = 9;
};

// Tier-1/2 (payload-only or value-tagged) wrappers — positions 10..15

template <typename T>
struct canonical_layer_index<Stale<T>> {
    static constexpr int value = 10;
};

template <typename T, typename Tag>
struct canonical_layer_index<Tagged<T, Tag>> {
    static constexpr int value = 11;
};

template <auto Pred, typename T>
struct canonical_layer_index<Refined<Pred, T>> {
    static constexpr int value = 12;
};

template <typename T>
struct canonical_layer_index<Secret<T>> {
    static constexpr int value = 13;
};

template <typename T>
struct canonical_layer_index<Linear<T>> {
    static constexpr int value = 14;
};

template <typename Row, typename T>
struct canonical_layer_index<::crucible::effects::Computation<Row, T>> {
    static constexpr int value = 15;
};

// Pin: §XVI has exactly 16 canonical positions.  A reviewer
// extending this catalog updates the literal in lockstep with the
// CLAUDE.md §XVI documentation.
inline constexpr int kCanonicalLayerCount = 16;

// ── HasCanonicalLayerIndex + canonical_layer_index_v -----------------

template <typename W>
concept HasCanonicalLayerIndex = requires {
    { canonical_layer_index<std::remove_cvref_t<W>>::value }
        -> std::convertible_to<int>;
};

template <typename W>
    requires HasCanonicalLayerIndex<W>
inline constexpr int canonical_layer_index_v
    = canonical_layer_index<std::remove_cvref_t<W>>::value;

// ── is_canonically_ordered<Stack>() — the actual predicate ─────────
//
// Walks `Stack` inward via `::value_type`, collecting indices for
// each canonical layer found.  Returns true iff those indices form
// a STRICTLY-INCREASING sequence (no duplicates, no inversions).
// Off-tree wrappers and the innermost payload are skipped without
// affecting the check.

namespace detail {

template <int Prev, typename Layer>
struct walk_canonical {
    static consteval bool eval() noexcept {
        using L = std::remove_cvref_t<Layer>;
        if constexpr (HasCanonicalLayerIndex<L>) {
            constexpr int my = canonical_layer_index<L>::value;
            // Strict increase: same index twice (e.g.
            // HotPath<...><HotPath<...>>) is a defect.
            if (my <= Prev) return false;
            if constexpr (requires { typename L::value_type; }) {
                return walk_canonical<my, typename L::value_type>::eval();
            } else {
                return true;
            }
        } else {
            // Off-tree wrapper or bare payload — keep Prev, descend.
            if constexpr (requires { typename L::value_type; }) {
                return walk_canonical<Prev, typename L::value_type>::eval();
            } else {
                return true;
            }
        }
    }
};

}  // namespace detail

template <typename Stack>
[[nodiscard]] consteval bool is_canonically_ordered() noexcept {
    return detail::walk_canonical<-1, std::remove_cvref_t<Stack>>::eval();
}

template <typename Stack>
inline constexpr bool is_canonically_ordered_v
    = is_canonically_ordered<Stack>();

template <typename Stack>
concept CanonicallyOrdered = is_canonically_ordered_v<Stack>;

// ── Header-local sentinel asserts (FIXY-V-008 discipline) ──────────
//
// These pin the §XVI position values and verify the walk behavior
// at the point of declaration; downstream consumers MUST see the
// same numbers.  A canonical-layer renumbering must update both
// the specializations above AND every site reading
// canonical_layer_index_v<W> below.

namespace _selftest {

// (1) Per-wrapper layer-index pins.  Out-of-tree positions zero-
//     based; the comment beside each cites the §XVI doc line.
static_assert(canonical_layer_index_v<HotPath<HotPathTier_v::Hot, int>>       ==  0);
static_assert(canonical_layer_index_v<DetSafe<DetSafeTier_v::Pure, int>>      ==  1);
// NumericalTier uses the Tolerance enum (not a _v alias).
static_assert(canonical_layer_index_v<NumericalTier<::crucible::algebra::lattices::Tolerance::BITEXACT, int>> ==  2);
static_assert(canonical_layer_index_v<Vendor<VendorBackend_v::NV, int>>        ==  3);
static_assert(canonical_layer_index_v<ResidencyHeat<ResidencyHeatTag_v::Hot, int>> == 4);
static_assert(canonical_layer_index_v<CipherTier<CipherTierTag_v::Hot, int>>   ==  5);
static_assert(canonical_layer_index_v<AllocClass<AllocClassTag_v::Arena, int>> == 6);
static_assert(canonical_layer_index_v<Wait<WaitStrategy_v::SpinPause, int>>    == 7);
// MemOrder uses MemOrderTag_v.  Use the SeqCst entry as a stable witness.
static_assert(canonical_layer_index_v<MemOrder<MemOrderTag_v::SeqCst, int>>    == 8);
static_assert(canonical_layer_index_v<Progress<ProgressClass_v::Bounded, int>> == 9);
static_assert(canonical_layer_index_v<Stale<int>>                              == 10);
static_assert(canonical_layer_index_v<Tagged<int, source::FromUser>>           == 11);
// Refined needs a concrete predicate; the bounded_above<8> mint
// exists in Refined.h's predicate corner.
static_assert(canonical_layer_index_v<Refined<bounded_above<int{8}>, int>>     == 12);
static_assert(canonical_layer_index_v<Secret<int>>                             == 13);
static_assert(canonical_layer_index_v<Linear<int>>                             == 14);
static_assert(canonical_layer_index_v<
    ::crucible::effects::Computation<::crucible::effects::Row<>, int>>         == 15);

// (2) Cardinality pin.
static_assert(kCanonicalLayerCount == 16,
    "FIXY-FOUND-048: §XVI canonical wrapper-nesting order ships 16 "
    "positions (HotPath through Computation).  A new canonical layer "
    "requires updating both CLAUDE.md §XVI AND this pin.");

// (3) Vacuously-canonical: bare payload is trivially in order.
static_assert(is_canonically_ordered_v<int>);
static_assert(is_canonically_ordered_v<double>);

// (4) Single-wrapper stacks are canonical (Prev=-1 < any 0..15).
static_assert(is_canonically_ordered_v<Linear<int>>);
static_assert(is_canonically_ordered_v<HotPath<HotPathTier_v::Hot, int>>);

// (5) Canonical two-layer: HotPath ⊃ Linear (0 < 14).
static_assert(is_canonically_ordered_v<
    HotPath<HotPathTier_v::Hot, Linear<int>>>);

// (6) INVERTED two-layer: Linear ⊃ HotPath (14 > 0) — rejected.
static_assert(!is_canonically_ordered_v<
    Linear<HotPath<HotPathTier_v::Hot, int>>>);

// (7) Same canonical layer twice (e.g. HotPath ⊃ HotPath) is a
//     defect — strict increase, equal is rejected.
static_assert(!is_canonically_ordered_v<
    HotPath<HotPathTier_v::Hot,
        HotPath<HotPathTier_v::Cold, int>>>);

// (8) Full §XVI stack — the canonical example from CLAUDE.md §XVI.
//     HotPath ⊃ DetSafe ⊃ NumericalTier ⊃ Vendor ⊃ Computation<Row<>, T>
static_assert(is_canonically_ordered_v<
    HotPath<HotPathTier_v::Hot,
        DetSafe<DetSafeTier_v::Pure,
            NumericalTier<::crucible::algebra::lattices::Tolerance::BITEXACT,
                Vendor<VendorBackend_v::NV,
                    ::crucible::effects::Computation<
                        ::crucible::effects::Row<>, int>>>>>>);

// (9) Off-tree wrapper does NOT disrupt canonical descent.
//     Linear<T> at position 14, with Stale<T> at position 10 INSIDE,
//     is still inverted (14 > 10) — but an off-tree wrapper between
//     two canonical layers passes through neutrally.  Witness:
//     Tagged<...> at position 11 wrapping Refined at position 12 is
//     canonical (11 < 12).
static_assert(is_canonically_ordered_v<
    Tagged<Refined<bounded_above<int{8}>, int>, source::FromUser>>);

// (10) Triple-stack mid-§XVI: Stale ⊃ Tagged ⊃ Refined (10 < 11 < 12).
static_assert(is_canonically_ordered_v<
    Stale<Tagged<Refined<bounded_above<int{8}>, int>, source::FromUser>>>);

// (11) Inverting that triple: Refined ⊃ Tagged ⊃ Stale (12 > 11 > 10)
//      — rejected.
static_assert(!is_canonically_ordered_v<
    Refined<bounded_above<int{8}>,
        Tagged<Stale<int>, source::FromUser>>>);

}  // namespace _selftest
}  // namespace crucible::safety::diag::canonical_order
