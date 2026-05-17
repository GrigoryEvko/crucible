#pragma once

// ── crucible::fixy::struct_ — Structural-wrapper re-exports ────────
//
// Phase C re-export.  Surfaces every NON-Graded "structural" wrapper
// under `fixy::struct_::` so callers who include only the fixy
// umbrella never have to descend into the safety/ tree to reach a
// structural primitive.  Companion to:
//
//   - fixy/Safety.h — Linear / Secret / ScopedView token mints
//   - fixy/Mach.h   — Machine token mint
//   - fixy/Perm.h   — Permission / SharedPermission token mints
//   - fixy/Wrap.h   — Graded-backed value wrappers
//   - fixy/Is.h     — Is*.h concept-gate aliases
//
// Per CLAUDE.md L0 §Safety, "structural wrappers — deliberately not
// graded" are RAII / typestate / structural-constraint disciplines
// that don't fit the `Graded<M, L, T>` shape.  This header re-exports
// the nine of them that have not already shipped via Safety.h /
// Mach.h:
//
//   1. Pinned<T> / NonMovable<T>     — address-stability mixins
//   2. NotInherited<T> / FinalBy<T> /
//      assert_not_inherited<T>()     — structural non-extensibility
//   3. Checked.h primitives          — overflow-checked arithmetic
//      (checked_*, wrapping_*, trapping_*, saturating_*, safe_*,
//       ensure_bytes_fit)
//   4. ct::select / ct::eq /
//      ct::mask_from_bit / ct::less /
//      ct::is_zero / ct::cswap       — branch-free crypto primitives
//   5. crucible::simd::* facade      — DetSafe SIMD primitives
//   6. OwnedRegion<T, Tag> +
//      parallel_for_views<N> /
//      parallel_reduce_views<N,R> /
//      parallel_apply_pair<N> /
//      parallel_for_views_adaptive /
//      parallel_for_smart            — Workload concurrency primitives
//
// Note the name `struct_` (trailing underscore) — `struct` is a C++
// keyword, so the canonical short name `struct` is unavailable.  This
// matches the standard convention used by every modern PL with a
// reserved-word collision (Python's `class_` / `def_`, Ruby's `def_`,
// etc.).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — every primitive default-constructs to a well-defined
//                state; aliases preserve.
//   TypeSafe   — using-declarations preserve concept gates
//                (std::integral T, std::unsigned_integral T,
//                 DetSafeSimd<V>, etc.).
//   NullSafe   — OwnedRegion carries (T*, count) with the invariant
//                "count == 0 iff base == nullptr"; alias preserves.
//   MemSafe    — Pinned/NonMovable delete copy AND move at compile
//                time; OwnedRegion is move-only by its embedded
//                Permission; aliases preserve.
//   BorrowSafe — Workload primitives encode the CSL fork-rule;
//                ct::* primitives are branch-free; aliases preserve.
//   ThreadSafe — Workload uses std::jthread + RAII join (happens-
//                before); aliases preserve.
//   LeakSafe   — every primitive is value-typed or stack-allocated;
//                no leak path.
//   DetSafe    — pure value primitives; bit-exact across re-export;
//                Simd facade pins integer-only reductions for
//                BITEXACT recipes.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives —
// `sizeof(fixy::struct_::X) == sizeof(safety::X)` for every X.  No
// runtime indirection, no extra branch, no extra storage.

#include <crucible/Saturate.h>
#include <crucible/safety/Checked.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Workload.h>

namespace crucible::fixy::struct_ {

// ── Pinned / NonMovable (address-stability mixins) ─────────────────

using ::crucible::safety::NonMovable;
using ::crucible::safety::Pinned;

// ── NotInherited / FinalBy (structural non-extensibility) ──────────

using ::crucible::safety::FinalBy;
using ::crucible::safety::NotInherited;
using ::crucible::safety::assert_not_inherited;

// ── Checked arithmetic (overflow-checked / wrapping / trapping /
//    saturating / compile-time safe_* / byte-budget) ───────────────

using ::crucible::safety::checked_abs;
using ::crucible::safety::checked_add;
using ::crucible::safety::checked_div;
using ::crucible::safety::checked_mod;
using ::crucible::safety::checked_mul;
using ::crucible::safety::checked_neg;
using ::crucible::safety::checked_shl;
using ::crucible::safety::checked_shr;
using ::crucible::safety::checked_sub;

using ::crucible::safety::wrapping_add;
using ::crucible::safety::wrapping_mul;
using ::crucible::safety::wrapping_sub;

using ::crucible::safety::trapping_add;
using ::crucible::safety::trapping_div;
using ::crucible::safety::trapping_mul;
using ::crucible::safety::trapping_sub;

using ::crucible::safety::saturating_add;
using ::crucible::safety::saturating_mul;
using ::crucible::safety::saturating_sub;

// Saturation primitives live in `crucible::sat` (Saturate.h); re-export
// the canonical names alongside the wrapper-tier `saturating_*` aliases
// for callers reaching into the lower layer directly.
using ::crucible::sat::add_sat;
using ::crucible::sat::mul_sat;
using ::crucible::sat::sub_sat;

// Compile-time variable templates (size-arithmetic, byte budgets).
using ::crucible::safety::bytes_fit_v;
using ::crucible::safety::ensure_bytes_fit;
using ::crucible::safety::safe_add;
using ::crucible::safety::safe_add_all;
using ::crucible::safety::safe_array_bytes;
using ::crucible::safety::safe_byte_budget;
using ::crucible::safety::safe_capacity;
using ::crucible::safety::safe_mul;
using ::crucible::safety::safe_size_diff;
using ::crucible::safety::safe_size_sum;
using ::crucible::safety::safe_struct_bytes;
using ::crucible::safety::safe_sub;

// ── ConstantTime primitives (branch-free crypto) ───────────────────
//
// Re-exported under their own ::ct sub-namespace to match the
// substrate's `crucible::safety::ct::*` convention.  Callers spell
// `fixy::struct_::ct::select(...)` exactly the way they'd spell
// `safety::ct::select(...)` — the only change is the include path.

namespace ct {

using ::crucible::safety::ct::cswap;
using ::crucible::safety::ct::eq;
using ::crucible::safety::ct::is_zero;
using ::crucible::safety::ct::less;
using ::crucible::safety::ct::mask_from_bit;
using ::crucible::safety::ct::select;

}  // namespace ct

// ── crucible::simd facade (DetSafe SIMD primitives) ────────────────
//
// Re-exported under ::simd sub-namespace to match the substrate's
// `crucible::simd::*` convention.  Width-pinned aliases + the
// DetSafeSimd concept + iota_v / prefix_mask + the microarch
// detection flags + runtime probes all survive the using-decl path.

namespace simd {

// Width-pinned vec aliases.
using ::crucible::simd::i32x16;
using ::crucible::simd::i32x8;
using ::crucible::simd::i64x4;
using ::crucible::simd::i64x8;
using ::crucible::simd::i64x8_mask;
using ::crucible::simd::u32x16;
using ::crucible::simd::u32x8;
using ::crucible::simd::u32x8_mask;
using ::crucible::simd::u64x4;
using ::crucible::simd::u64x8;
using ::crucible::simd::u64x8_mask;
using ::crucible::simd::u8x16;
using ::crucible::simd::u8x32;

// Concept gate for BITEXACT-eligible vec types.
template <typename V>
concept DetSafeSimd = ::crucible::simd::DetSafeSimd<V>;

// Primitives std::simd doesn't ship.
using ::crucible::simd::iota_v;
using ::crucible::simd::prefix_mask;

// Compile-time microarch detection.
using ::crucible::simd::kAvx2Available;
using ::crucible::simd::kAvx512Available;
using ::crucible::simd::kNeonAvailable;
using ::crucible::simd::kSse42Available;

// Runtime microarch detection.
using ::crucible::simd::runtime_supports_avx2;
using ::crucible::simd::runtime_supports_avx512;
using ::crucible::simd::runtime_supports_sse42;

}  // namespace simd

// ── OwnedRegion + Workload primitives ──────────────────────────────
//
// OwnedRegion<T, Tag> is the contiguous-buffer model that substitutes
// for Rust's borrow checker.  Workload.h's parallel_for_views family
// is the user-facing concurrency layer that consumes OwnedRegions.

using ::crucible::safety::OwnedRegion;
using ::crucible::safety::Slice;

using ::crucible::safety::log_topology_at_startup;
using ::crucible::safety::parallel_apply_pair;
using ::crucible::safety::parallel_for_smart;
using ::crucible::safety::parallel_for_views;
using ::crucible::safety::parallel_for_views_adaptive;
using ::crucible::safety::parallel_reduce_views;
using ::crucible::safety::should_parallelize;
using ::crucible::safety::WorkBudget;

}  // namespace crucible::fixy::struct_
