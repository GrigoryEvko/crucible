#pragma once
//
// safety/LocalityHint.h — type-level placement preference
// (FOUND-E14/E15 per 27_04 §5.18).
//
// ═════════════════════════════════════════════════════════════════════
// Pattern (27_04 §5.18)
// ═════════════════════════════════════════════════════════════════════
//
// `concurrent/ParallelismRule.h::recommend_parallelism` derives a
// default NumaPolicy from the cache-tier classification:
//   L1/L2-resident → NumaIgnore (sequential)
//   L3-resident    → NumaLocal  (intra-socket)
//   DRAM-bound     → NumaSpread (across nodes) OR NumaIgnore
//
// Some user data has a STRONGER placement preference than the cache
// rule can derive — e.g., a per-NUMA-node arena's data wants a
// node-local pinning even if it falls in the DRAM tier; an inter-
// node messaging buffer wants a spread placement regardless of
// tier.  This header lets a Tag opt in to a preferred policy via a
// typedef.
//
// NAMING NOTE: this header was previously `NumaPreference.h`.  The
// concept covers more than NUMA in the long run — cache-line
// affinity, hyperthread placement, container-CPU pinning all live
// in the same conceptual space (a value's PHYSICAL placement
// preference, separate from its computational role).  Renamed to
// `LocalityHint` so future extensions for non-NUMA placement
// dimensions can absorb naturally.  The current implementation
// only routes the hint into NumaPolicy; that's the today story.
//
// ═════════════════════════════════════════════════════════════════════
// Surface
// ═════════════════════════════════════════════════════════════════════
//
//   * LocalityLocal_t / LocalitySpread_t / LocalityIgnore_t —
//     phantom tag types (one per NUMA-policy intent today; future
//     extensions may add LocalityCacheLine_t etc.).  Empty classes;
//     sizeof = 1, EBO-collapsible to 0.
//
//   * HasLocalityHint<Tag> — concept matching Tags that carry a
//     `locality_hint` typedef set to one of the three phantoms.
//
//   * locality_hint_of_v<Tag> — extracts the Tag's preference as a
//     concurrent::NumaPolicy value.  Returns NumaIgnore for Tags
//     without the typedef (so consumers default safely).
//
//   * recommend_parallelism_with_locality<Tag>(budget) — wrapper
//     around concurrent::recommend_parallelism that applies the
//     Tag's locality hint (if any) to the returned decision.
//
// ═════════════════════════════════════════════════════════════════════
// Worked example
// ═════════════════════════════════════════════════════════════════════
//
// User declares a Tag that opts in to node-local pinning:
//
//   struct PerNodeWeights {
//       using locality_hint =
//           ::crucible::safety::LocalityLocal_t;
//   };
//
// The parallelism rule will return NumaLocal for OwnedRegion<float,
// PerNodeWeights> regardless of the working-set size.  A Tag
// without `locality_hint` falls back to the cache-tier rule's
// default — no opt-out cost for Tags that don't care.

#include <crucible/Platform.h>
#include <crucible/concurrent/ParallelismRule.h>

#include <type_traits>

namespace crucible::safety {

// ═════════════════════════════════════════════════════════════════════
// ── Phantom tag types ───────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Today's three intents map 1-to-1 onto concurrent::NumaPolicy.
// Future extensions (LocalityCacheLine_t, LocalityHyperthread_t,
// LocalityContainerCpu_t, …) live alongside; the dispatch layer
// chooses how to consume each.
//
// The phantoms are empty — sizeof = 1, EBO-collapsible to 0 in
// composing structs.  No operator overloads, no behavior — purely
// a type-level marker.

struct LocalityIgnore_t {};
struct LocalityLocal_t  {};
struct LocalitySpread_t {};

// ═════════════════════════════════════════════════════════════════════
// ── HasLocalityHint concept ────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// True iff Tag has a `locality_hint` typedef set to ONE of the
// three phantom types.  The typedef MUST resolve to exactly one of
// {LocalityIgnore_t, LocalityLocal_t, LocalitySpread_t}; an
// arbitrary type fails the concept (catches typos like `Local_t`
// instead of `LocalityLocal_t`).

template <typename Tag>
concept HasLocalityHint = requires {
    typename Tag::locality_hint;
    requires std::is_same_v<typename Tag::locality_hint, LocalityIgnore_t>
          || std::is_same_v<typename Tag::locality_hint, LocalityLocal_t>
          || std::is_same_v<typename Tag::locality_hint, LocalitySpread_t>;
};

// ═════════════════════════════════════════════════════════════════════
// ── locality_hint_of_v<Tag> — extract preference at compile time ───
// ═════════════════════════════════════════════════════════════════════
//
// Returns the Tag's preference as a concurrent::NumaPolicy value.
// For Tags without the typedef, returns NumaIgnore — the safest
// default (lets the parallelism rule's cache-tier classification
// decide).
//
// Implementation: dispatch on HasLocalityHint, then on the typedef's
// identity.  All branches collapse to a single constant at compile
// time.

namespace detail {

template <typename Tag>
[[nodiscard]] consteval ::crucible::concurrent::NumaPolicy
locality_hint_of_impl() noexcept {
    if constexpr (HasLocalityHint<Tag>) {
        using H = typename Tag::locality_hint;
        if constexpr (std::is_same_v<H, LocalityLocal_t>) {
            return ::crucible::concurrent::NumaPolicy::NumaLocal;
        } else if constexpr (std::is_same_v<H, LocalitySpread_t>) {
            return ::crucible::concurrent::NumaPolicy::NumaSpread;
        } else {
            // H == LocalityIgnore_t (the third concept disjunct).
            return ::crucible::concurrent::NumaPolicy::NumaIgnore;
        }
    } else {
        return ::crucible::concurrent::NumaPolicy::NumaIgnore;
    }
}

}  // namespace detail

template <typename Tag>
inline constexpr ::crucible::concurrent::NumaPolicy
locality_hint_of_v = detail::locality_hint_of_impl<Tag>();

// ═════════════════════════════════════════════════════════════════════
// ── recommend_parallelism_with_locality<Tag>(budget) ───────────────
// ═════════════════════════════════════════════════════════════════════
//
// Calls the underlying parallelism rule, then applies the Tag's
// locality hint (if any) to the decision.  For Tags without a
// hint, returns the unchanged decision.
//
// Override discipline: the Tag's hint WINS over the cache-tier-
// derived default.  The parallelism rule still decides Sequential
// vs Parallel and the factor — only the NumaPolicy is overridden.
// This preserves the no-regression rule (small workloads still go
// Sequential regardless of hint).
//
// Pattern at a dispatch site:
//
//   const auto dec = recommend_parallelism_with_locality<MyTag>(
//       WorkBudget{ .read_bytes = ws, .write_bytes = ws,
//                   .item_count = n });
//   // dec.numa now reflects MyTag::locality_hint (if any),
//   // or the cache-tier default (if MyTag has no hint).

template <typename Tag>
[[nodiscard]] inline ::crucible::concurrent::ParallelismDecision
recommend_parallelism_with_locality(
    ::crucible::concurrent::WorkBudget budget) noexcept {
    auto dec = ::crucible::concurrent::recommend_parallelism(budget);
    if constexpr (HasLocalityHint<Tag>) {
        // Tag's hint overrides the cache-tier-derived default.
        // Sequential decisions keep NumaIgnore (no workers to place).
        if (dec.kind == ::crucible::concurrent::ParallelismDecision::Kind::Parallel) {
            dec.numa = locality_hint_of_v<Tag>;
        }
    }
    return dec;
}

// ═════════════════════════════════════════════════════════════════════
// ── Self-test ─────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════

namespace detail::locality_hint_self_test {

// ── Tags without hint fall back to NumaIgnore ─────────────────────

struct unpinned_tag {};
static_assert(!HasLocalityHint<unpinned_tag>);
static_assert(locality_hint_of_v<unpinned_tag>
              == ::crucible::concurrent::NumaPolicy::NumaIgnore);

// ── Tag with LocalityLocal_t hint ────────────────────────────────

struct local_tag {
    using locality_hint = LocalityLocal_t;
};
static_assert(HasLocalityHint<local_tag>);
static_assert(locality_hint_of_v<local_tag>
              == ::crucible::concurrent::NumaPolicy::NumaLocal);

// ── Tag with LocalitySpread_t hint ───────────────────────────────

struct spread_tag {
    using locality_hint = LocalitySpread_t;
};
static_assert(HasLocalityHint<spread_tag>);
static_assert(locality_hint_of_v<spread_tag>
              == ::crucible::concurrent::NumaPolicy::NumaSpread);

// ── Tag with LocalityIgnore_t hint (explicit) ────────────────────

struct ignore_tag {
    using locality_hint = LocalityIgnore_t;
};
static_assert(HasLocalityHint<ignore_tag>);
static_assert(locality_hint_of_v<ignore_tag>
              == ::crucible::concurrent::NumaPolicy::NumaIgnore);

// ── Tag with WRONG locality_hint type fails the concept ──────────

struct typo_tag {
    using locality_hint = int;  // not one of the three phantoms
};
static_assert(!HasLocalityHint<typo_tag>);
static_assert(locality_hint_of_v<typo_tag>
              == ::crucible::concurrent::NumaPolicy::NumaIgnore);

// ── Phantoms are empty (sizeof = 1, EBO-collapsible) ─────────────

static_assert(std::is_empty_v<LocalityIgnore_t>);
static_assert(std::is_empty_v<LocalityLocal_t>);
static_assert(std::is_empty_v<LocalitySpread_t>);

}  // namespace detail::locality_hint_self_test

}  // namespace crucible::safety
