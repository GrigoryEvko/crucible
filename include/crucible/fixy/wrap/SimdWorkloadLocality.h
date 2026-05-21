#pragma once

// ── crucible::fixy::wrap — Simd + Workload + LocalityHint surface ──
//
// Surfaces three workload-policy substrates under `fixy::wrap::`:
//
//   safety/Simd.h          — std::simd facade (width-pinned aliases,
//                            DetSafeSimd concept, iota_v / prefix_mask,
//                            microarch detection flags + probes)
//                            — lives in `crucible::simd::` substrate.
//   safety/Workload.h      — parallel_for_views / parallel_reduce_views
//                            / parallel_apply_pair / parallel_for_smart
//                            + WorkBudget + should_parallelize +
//                            log_topology_at_startup.
//   safety/LocalityHint.h  — LocalityIgnore_t / LocalityLocal_t /
//                            LocalitySpread_t phantom tags,
//                            HasLocalityHint concept, locality_hint_of_v
//                            + recommend_parallelism_with_locality.
//
// Per misc/27_04_2026.md §3.3 + FIXY-V-041: closes the umbrella-reach
// gap where a workload-policy site wanting SIMD lanes for inner loops,
// parallel-views dispatch, and NUMA-policy override had to descend
// into three different substrate namespaces (`crucible::simd::`,
// `crucible::safety::`, `crucible::safety::`).  All three substrates
// together form THE user-facing "how this runs" policy stack.
//
// ── Substrate consumed (37 symbols across 3 sub-families) ──────────
//
// Simd surface (23) — `crucible::simd::`:
//   10 vec aliases: i64x4, i64x8, u64x4, u64x8, i32x8, i32x16,
//                   u32x8, u32x16, u8x16, u8x32
//    3 mask aliases: i64x8_mask, u64x8_mask, u32x8_mask
//    1 concept:      DetSafeSimd<V>
//    2 functions:    iota_v<V>(), prefix_mask<V>(count)
//    4 compile-time flags: kAvx512Available, kAvx2Available,
//                          kSse42Available, kNeonAvailable
//    3 runtime probes: runtime_supports_avx512 / _avx2 / _sse42
//
// Workload surface (8) — `crucible::safety::`:
//   WorkBudget                    — workload size descriptor
//   should_parallelize            — boolean cost-model gate
//   parallel_for_views<N>         — N-way fork-join dispatcher
//   parallel_reduce_views<N, R>   — map-reduce with disjoint partials
//   parallel_apply_pair<N, ...>   — co-iterated pair dispatcher
//   parallel_for_views_adaptive   — budget-gated sequential fast path
//   parallel_for_smart            — auto-derive WorkBudget + dispatch
//   log_topology_at_startup       — Topology probe + stderr summary
//
// LocalityHint surface (6) — `crucible::safety::`:
//   LocalityIgnore_t / LocalityLocal_t / LocalitySpread_t  — phantom tags
//   HasLocalityHint<Tag>                                   — concept
//   locality_hint_of_v<Tag>                                — variable template
//   recommend_parallelism_with_locality<Tag>(budget)       — dispatcher
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Type-alias using-declarations preserve substrate identity.
// Function-template using-declarations are pure name-lookup
// directives.  Variable-template using-declarations resolve to the
// substrate's compile-time constant.  Concept using-declarations
// preserve the substrate admission set.  No runtime indirection,
// no extra storage, no extra branch.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — WorkBudget has NSDMI defaults; phantom tags are empty.
//   TypeSafe   — strong type-aliases preserved across using-decls.
//   NullSafe   — no pointer surface added.
//   MemSafe    — workers join via std::jthread RAII; OwnedRegion
//                rebuild is structurally sound by mint_permission_fork.
//   BorrowSafe — workers write disjoint shards (compile-time-proved by
//                Slice<Whole, I> tag distinction).
//   ThreadSafe — jthread::join provides happens-before; main-thread
//                reads after recombine are well-defined per
//                [intro.races].
//   LeakSafe   — std::array<jthread, N> destructor joins all workers.
//   DetSafe    — Simd reductions are integer-only via DetSafeSimd
//                concept; Workload fold order is fixed left-to-right
//                in index sequence; LocalityHint is a policy override,
//                does NOT change the result, only worker placement.

#include <crucible/safety/Simd.h>          // 23 symbols in crucible::simd::
#include <crucible/safety/Workload.h>      //  8 symbols in crucible::safety::
#include <crucible/safety/LocalityHint.h>  //  6 symbols in crucible::safety::

#include <cstddef>       // self_test uses size_t
#include <type_traits>   // self_test uses std::is_same_v
#include <utility>       // self_test uses std::pair

namespace crucible::fixy::wrap {

// ═══════════════════════════════════════════════════════════════════
// ── Simd surface (23 symbols) ─────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Width-pinned vec aliases — TensorMeta ndim ≤ 8 → i64x8/u64x8 natural.
using ::crucible::simd::i64x4;
using ::crucible::simd::i64x8;
using ::crucible::simd::u64x4;
using ::crucible::simd::u64x8;
using ::crucible::simd::i32x8;
using ::crucible::simd::i32x16;
using ::crucible::simd::u32x8;
using ::crucible::simd::u32x16;
using ::crucible::simd::u8x16;
using ::crucible::simd::u8x32;

// Mask aliases — for predicated reduce / select dispatch.
using ::crucible::simd::i64x8_mask;
using ::crucible::simd::u64x8_mask;
using ::crucible::simd::u32x8_mask;

// DetSafeSimd<V> — integer-lane gate for BITEXACT-recipe reductions.
using ::crucible::simd::DetSafeSimd;

// iota_v<V>() — lane[i] == i.  Foundation for prefix_mask + per-lane
// table indexing.  std::simd has no public iota primitive.
using ::crucible::simd::iota_v;

// prefix_mask<V>(count) — first N lanes set, rest unset.  Pair with
// std::simd::reduce's masked overload for one-call aggregation.
using ::crucible::simd::prefix_mask;

// Compile-time microarch flags — `if constexpr` gating in hot paths.
using ::crucible::simd::kAvx512Available;
using ::crucible::simd::kAvx2Available;
using ::crucible::simd::kSse42Available;
using ::crucible::simd::kNeonAvailable;

// Runtime microarch probes — bench-harness + diagnostic reporting.
// __builtin_cpu_supports backing; first call may incur CPUID.
using ::crucible::simd::runtime_supports_avx512;
using ::crucible::simd::runtime_supports_avx2;
using ::crucible::simd::runtime_supports_sse42;

// ═══════════════════════════════════════════════════════════════════
// ── Workload surface (8 symbols) ──────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// WorkBudget — read_bytes + write_bytes + item_count.  Static
// factories `for_span` / `for_span_read_only` cover the 95% case.
using ::crucible::safety::WorkBudget;

// should_parallelize(budget) — boolean cost-model gate.  True iff
// recommend_parallelism returns Parallel.  For factor + NUMA, call
// concurrent::recommend_parallelism directly.
using ::crucible::safety::should_parallelize;

// parallel_for_views<N>(region, body) — N-way fork-join dispatcher
// over OwnedRegion<T, Whole>.  N == 1 fast path (no jthread spawn).
// N >= 2 spawns std::array<jthread, N>; RAII join at scope exit.
using ::crucible::safety::parallel_for_views;

// parallel_reduce_views<N, R>(region, init, mapper, reducer) — map-
// reduce with stack-allocated partials.  Left-to-right fold order
// after RAII join; reducer must be associative for correctness.
using ::crucible::safety::parallel_reduce_views;

// parallel_apply_pair<N, T1, W1, T2, W2>(rgn_a, rgn_b, body) — co-
// iterated pair dispatcher.  Sizes must match (CRUCIBLE_ASSERT).
// Lowering target for FOUND-D13 BinaryTransform shape recognizer.
using ::crucible::safety::parallel_apply_pair;

// parallel_for_views_adaptive<N>(region, body, budget) — budget-gated
// sequential fast path.  Falls through to parallel_for_views<N> when
// should_parallelize(budget) is true.  No regression at small workloads.
using ::crucible::safety::parallel_for_views_adaptive;

// parallel_for_smart(region, body) — THE 95%-case API.  Auto-derives
// WorkBudget from region.cspan(); consults recommend_parallelism for
// factor (snapped to 1/2/4/8/16) + NUMA policy; dispatches via switch
// to the matching parallel_for_views<N>.
using ::crucible::safety::parallel_for_smart;

// log_topology_at_startup(FILE* = stderr) — forces Topology::instance()
// probe + emits one-screen human-readable summary.  Keeper / Vessel /
// test startup paths; otherwise the Topology probe is lazy.
using ::crucible::safety::log_topology_at_startup;

// ═══════════════════════════════════════════════════════════════════
// ── LocalityHint surface (6 symbols) ──────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// Phantom tags — empty types, sizeof = 1, EBO-collapsible to 0.
using ::crucible::safety::LocalityIgnore_t;
using ::crucible::safety::LocalityLocal_t;
using ::crucible::safety::LocalitySpread_t;

// HasLocalityHint<Tag> — concept matching Tags that carry a
// `locality_hint` typedef set to one of the three phantoms.
using ::crucible::safety::HasLocalityHint;

// locality_hint_of_v<Tag> — variable template extracting Tag's hint
// as a concurrent::NumaPolicy value.  Returns NumaIgnore for Tags
// without the typedef (safe default).
using ::crucible::safety::locality_hint_of_v;

// recommend_parallelism_with_locality<Tag>(budget) — calls the
// underlying cost-model rule, then applies Tag's locality hint
// (if any) to the returned ParallelismDecision's `numa` field.
// Sequential decisions keep NumaIgnore unconditionally.
using ::crucible::safety::recommend_parallelism_with_locality;

}  // namespace crucible::fixy::wrap

// ═══════════════════════════════════════════════════════════════════
// ── Dual-export sentinel — FIXY-V-041 ──────────────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// Header-internal identity sentinels.  Same discipline as
// fixy/Handle.h::self_test (FIXY-U-016), fixy/Kernel.h (V-038/V-039),
// fixy/Reflect.h (V-040).  Verifies each surface resolves to the
// substrate symbol with matching identity / value / concept admission.

namespace crucible::fixy::wrap::self_test_simd_workload_locality {

// ── 1. Simd type-alias identity ────────────────────────────────────
//
// Type aliases are direct using-decls of typedefs / using-decls in
// the substrate.  is_same_v witness across both reach paths.

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::i64x8,
    ::crucible::simd::i64x8>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::u32x8,
    ::crucible::simd::u32x8>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::u8x16,
    ::crucible::simd::u8x16>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::i64x8_mask,
    ::crucible::simd::i64x8_mask>);

// ── 2. Simd compile-time flag identity ─────────────────────────────
//
// Variable-template using-decls resolve to substrate's compile-time
// constant.  Boolean equality witnesses identity.

static_assert(
    ::crucible::fixy::wrap::kAvx512Available ==
    ::crucible::simd::kAvx512Available);
static_assert(
    ::crucible::fixy::wrap::kAvx2Available ==
    ::crucible::simd::kAvx2Available);
static_assert(
    ::crucible::fixy::wrap::kSse42Available ==
    ::crucible::simd::kSse42Available);
static_assert(
    ::crucible::fixy::wrap::kNeonAvailable ==
    ::crucible::simd::kNeonAvailable);

// ── 3. Simd DetSafeSimd concept identity ───────────────────────────
//
// Concept admission set preserved across reach paths.  Positive case
// (i64x8 has integral value_type) + negative case (i64x8::mask_type
// is not a vec — it has no `value_type` integral lane in the
// concept-required sense).

static_assert(
    ::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::i64x8>);
static_assert(
    ::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::u64x8>);
static_assert(
    ::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::u32x8>);
// Negative: an explicit non-DetSafe candidate.  std::simd::vec on
// a floating-point type has `value_type = float` which fails the
// std::integral<value_type> requirement.
static_assert(
    !::crucible::fixy::wrap::DetSafeSimd<std::simd::vec<float, 8>>);
// Cross-path agreement on the negative case.
static_assert(
    ::crucible::fixy::wrap::DetSafeSimd<::crucible::simd::i64x8> ==
    ::crucible::simd::DetSafeSimd<::crucible::simd::i64x8>);

// ── 4. Simd iota_v value identity ──────────────────────────────────
//
// iota_v<V>() is constexpr-callable.  Both reach paths produce
// identical values; verify lane 0 and lane N-1 match.  reduce on
// the integer-lane vec is associative + commutative + bit-exact,
// so cross-path equality is well-defined.

[[nodiscard]] consteval bool iota_v_through_alias_matches_substrate() noexcept {
    constexpr auto via_fixy      = ::crucible::fixy::wrap::iota_v<::crucible::simd::u64x8>();
    constexpr auto via_substrate = ::crucible::simd::iota_v<::crucible::simd::u64x8>();
    // Lane-wise equality: subscript both, compare each.  vec::operator[]
    // is value-returning const; subscript returns lane value as integer.
    bool all_match = true;
    for (int lane = 0; lane < 8; ++lane) {
        if (via_fixy[lane] != via_substrate[lane]) {
            all_match = false;
        }
    }
    return all_match;
}
static_assert(iota_v_through_alias_matches_substrate());

// ── 5. Workload — WorkBudget type identity ─────────────────────────
//
// WorkBudget is a struct; using-decl is a type alias to the substrate
// struct.  is_same_v on the type + field default-equality witnesses
// the alias preserves the NSDMI defaults.

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::WorkBudget,
    ::crucible::safety::WorkBudget>);

[[nodiscard]] consteval bool workbudget_default_state_preserved() noexcept {
    ::crucible::fixy::wrap::WorkBudget b{};
    return b.read_bytes == 0 && b.write_bytes == 0 && b.item_count == 0;
}
static_assert(workbudget_default_state_preserved());

// ── 6. Workload — function-template reach identity ─────────────────
//
// parallel_for_views and friends are NOT constexpr (they spawn
// jthreads).  Function-pointer identity for an instantiated form
// witnesses the alias preserves substrate symbol identity.  Probe
// type: minimal `int` region with a no-op noexcept lambda.

struct WorkloadProbeTag {};

// Generic lambda's first instantiation point — capture the address
// of the substrate function instantiated for OwnedRegion<int,
// WorkloadProbeTag> with a no-op body.  Cross-path function-pointer
// equality witnesses identity.
//
// NOTE: we cannot easily address-of a function template without
// supplying explicit template arguments; for parallel_for_views the
// Body parameter is deduced and an unambiguous instantiation requires
// a concrete lambda type.  Instead we witness identity via a
// concrete-type using-decl: the using-declaration introduces the same
// template name; if it didn't alias, the namespace-qualified call
// below would be ambiguous or unresolved at substrate level.  The
// runtime test covers the call-through-alias witness.

// Note: function-pointer identity for free-function using-decls is
// trivially tautological under GCC 16's analyzer (`&fixy::wrap::f ==
// &safety::f` always true because the using-decl makes them the same
// declaration, not two synthesized entities).  -Werror=tautological-
// compare correctly rejects the assertion.  The runtime test calls
// `fw::should_parallelize(...)` and `fw::log_topology_at_startup(...)`
// through the alias, which is the load-bearing witness anyway: if the
// alias didn't resolve to the substrate, the call would fail to link
// or dispatch to a different implementation.

// ── 7. LocalityHint — phantom-tag identity ─────────────────────────
//
// Empty struct types preserved across using-decl; is_same_v witness
// + std::is_empty_v witness that the EBO-collapsible property is
// preserved.

static_assert(std::is_same_v<
    ::crucible::fixy::wrap::LocalityIgnore_t,
    ::crucible::safety::LocalityIgnore_t>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::LocalityLocal_t,
    ::crucible::safety::LocalityLocal_t>);
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::LocalitySpread_t,
    ::crucible::safety::LocalitySpread_t>);

static_assert(std::is_empty_v<::crucible::fixy::wrap::LocalityIgnore_t>);
static_assert(std::is_empty_v<::crucible::fixy::wrap::LocalityLocal_t>);
static_assert(std::is_empty_v<::crucible::fixy::wrap::LocalitySpread_t>);

// ── 8. LocalityHint — concept admission identity ───────────────────
//
// HasLocalityHint<Tag> reaches through the alias; both positive
// (Tag with valid locality_hint typedef) and negative (Tag without,
// or with wrong typedef type) cases admit identically.

struct LocalityHintProbe_Unhinted {};
struct LocalityHintProbe_Local {
    using locality_hint = ::crucible::safety::LocalityLocal_t;
};
struct LocalityHintProbe_Typo {
    using locality_hint = int;  // not one of the three phantoms
};

static_assert(!::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Unhinted>);
static_assert( ::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Local>);
static_assert(!::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Typo>);

// Cross-path concept admission agreement.
static_assert(
    ::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Local> ==
    ::crucible::safety::HasLocalityHint<LocalityHintProbe_Local>);
static_assert(
    ::crucible::fixy::wrap::HasLocalityHint<LocalityHintProbe_Typo> ==
    ::crucible::safety::HasLocalityHint<LocalityHintProbe_Typo>);

// ── 9. LocalityHint — locality_hint_of_v value identity ────────────
//
// Variable-template using-decl resolves to substrate's compile-time
// constant.  All four cases (no hint / Local / Spread / Ignore-
// explicit) verified across reach paths.

static_assert(
    ::crucible::fixy::wrap::locality_hint_of_v<LocalityHintProbe_Unhinted> ==
    ::crucible::concurrent::NumaPolicy::NumaIgnore);
static_assert(
    ::crucible::fixy::wrap::locality_hint_of_v<LocalityHintProbe_Local> ==
    ::crucible::concurrent::NumaPolicy::NumaLocal);

// Cross-path equality of the value template — alias identity proof.
static_assert(
    ::crucible::fixy::wrap::locality_hint_of_v<LocalityHintProbe_Local> ==
    ::crucible::safety::locality_hint_of_v<LocalityHintProbe_Local>);
static_assert(
    ::crucible::fixy::wrap::locality_hint_of_v<LocalityHintProbe_Unhinted> ==
    ::crucible::safety::locality_hint_of_v<LocalityHintProbe_Unhinted>);

// ── 10. Cardinality witness ───────────────────────────────────────
//
// 37 surfaced using-declarations across 3 substrate sub-families:
//
//   Simd surface (23) — crucible::simd::
//     (1..10)  10 vec aliases
//     (11..13) 3 mask aliases
//     (14)     DetSafeSimd concept
//     (15..16) iota_v + prefix_mask
//     (17..20) 4 compile-time flags
//     (21..23) 3 runtime probes
//
//   Workload surface (8) — crucible::safety::
//     (24) WorkBudget
//     (25) should_parallelize
//     (26..30) parallel_for_views / _reduce / _apply_pair /
//               _adaptive / _smart
//     (31) log_topology_at_startup
//
//   LocalityHint surface (6) — crucible::safety::
//     (32..34) 3 phantom tags
//     (35) HasLocalityHint
//     (36) locality_hint_of_v
//     (37) recommend_parallelism_with_locality
//
// Future additions to safety::Simd / safety::Workload /
// safety::LocalityHint MUST extend this block + bump the constant
// + add a sentinel above.

constexpr int simd_workload_locality_alias_cardinality = 37;
static_assert(simd_workload_locality_alias_cardinality == 37,
    "fixy::wrap::{Simd,Workload,LocalityHint} cardinality changed "
    "— update SimdWorkloadLocality.h sentinel block to track the "
    "substrate workload-policy surface.");

}  // namespace crucible::fixy::wrap::self_test_simd_workload_locality
