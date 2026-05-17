#pragma once

// ── crucible::fixy::wrap — Value wrappers under fixy:: ─────────────
//
// Phase C re-export.  Surfaces every remaining value-level safety
// wrapper under `fixy::wrap::` so callers who include only the fixy
// umbrella never have to descend into the safety/ tree to wrap a
// value.  Companion to:
//
//   - fixy/Safety.h — Linear / Secret / ScopedView token mints
//   - fixy/Perm.h   — Permission / SharedPermission token mints
//   - fixy/Mach.h   — Machine token mint
//
// This header covers the 11 canonical Graded-backed wrappers
// (CLAUDE.md L0 §Safety) plus the 5 Mutation.h derivative wrappers.
// Three of the eleven already ship via Safety.h / Perm.h; they are
// re-exported here too so `fixy::wrap::` is the single one-stop
// directory for value-wrapping.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
// Graded-backed (11 wrappers):
//   safety::Linear<T>                       — move-only linear carrier
//   safety::Refined<Pred, T>                — predicate-checked carrier
//   safety::SealedRefined<Pred, T>          — Refined with no into()
//   safety::Tagged<T, Source>               — provenance / trust tag
//   safety::Secret<T>                       — classified-by-default
//   safety::Monotonic<T, Cmp>               — only-advance value
//   safety::AppendOnly<T, Storage>          — grow-only container
//   safety::Stale<T>                        — value + staleness τ
//   safety::TimeOrdered<T, N, Tag>          — value + vector clock
//   safety::SharedPermission<Tag>           — fractional permission
//   (Linear / Secret / SharedPermission already shipped via Safety/Perm)
//
// Mutation.h derivative wrappers (5):
//   safety::WriteOnce<T>                    — settable exactly once
//   safety::WriteOnceNonNull<T*>            — pointer-slot, no opt tag
//   safety::BoundedMonotonic<T, Max, Cmp>   — Monotonic + upper bound
//   safety::OrderedAppendOnly<T, ...>       — AppendOnly + key order
//   safety::AtomicMonotonic<T, Cmp>         — thread-safe Monotonic
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — substrate forwards args; alias preserves NSDMI
//                discipline (every wrapper default-constructs to a
//                well-defined state).
//   TypeSafe   — using-declarations preserve concept gates
//                (PredicateInvocableOn, std::is_pointer_v, etc.).
//   NullSafe   — WriteOnceNonNull's nullptr-sentinel discipline is
//                load-bearing; using-declaration preserves it.
//   MemSafe    — Linear is move-only; alias preserves =delete.
//   BorrowSafe — Stale / TimeOrdered carry happens-before / staleness
//                in the type; alias preserves.
//   ThreadSafe — AtomicMonotonic is Pinned<>; SharedPermission is
//                refcounted; aliases preserve both.
//   LeakSafe   — every wrapper is value-typed; no leak path.
//   DetSafe    — pure value wraps; bit-exact across re-export.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives —
// `sizeof(fixy::wrap::X<T>) == sizeof(safety::X<T>)` for every X.
// No runtime indirection, no extra branch, no extra storage.
//
// ── Universal Mint Pattern surface ──────────────────────────────────
//
// Only Linear / Secret / SharedPermission expose a `mint_*` factory
// today (their `requires` clause encodes the load-bearing token-mint
// gate).  The other wrappers construct directly via their `explicit`
// ctor; the `requires` lives on the wrapped predicate / lattice /
// modality and is enforced at construction site without a free
// helper.  When a wrapper grows a mint factory, add a using-declaration
// here.

#include <crucible/permissions/Permission.h>  // SharedPermission
#include <crucible/safety/Linear.h>
#include <crucible/safety/Mutation.h>          // AppendOnly / Monotonic /
                                               // WriteOnce / WriteOnceNonNull /
                                               // BoundedMonotonic /
                                               // OrderedAppendOnly /
                                               // AtomicMonotonic
#include <crucible/safety/Refined.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/TimeOrdered.h>

namespace crucible::fixy::wrap {

// ─── Graded-backed wrappers (11) ─────────────────────────────────

// Linear<T> — move-only consume-once.
using ::crucible::safety::Linear;
using ::crucible::safety::mint_linear;
using ::crucible::safety::drop;

// Refined<Pred, T> — predicate-checked at construction.
using ::crucible::safety::Refined;
// Named refinement aliases — load-bearing per CLAUDE.md §XVI.
using ::crucible::safety::NonNull;
using ::crucible::safety::Positive;
using ::crucible::safety::NonNegative;
using ::crucible::safety::PowerOfTwo;
// Refined composition with Linear (both orderings).
using ::crucible::safety::LinearRefined;
using ::crucible::safety::RefinedLinear;
// Common stateless predicates — usable as Refined NTTP.
using ::crucible::safety::positive;
using ::crucible::safety::non_negative;
using ::crucible::safety::non_zero;
using ::crucible::safety::non_null;
using ::crucible::safety::power_of_two;
using ::crucible::safety::non_empty;
// Parameterised predicate templates.
using ::crucible::safety::Aligned;
using ::crucible::safety::InRange;
using ::crucible::safety::BoundedAbove;
using ::crucible::safety::LengthGe;
using ::crucible::safety::aligned;
using ::crucible::safety::in_range;
using ::crucible::safety::bounded_above;
using ::crucible::safety::length_ge;
// Cross-predicate implication trait — used by SessionPayloadSubsort.
using ::crucible::safety::predicate_implies;
using ::crucible::safety::implies_v;

// SealedRefined<Pred, T> — Refined with no into() extractor.
using ::crucible::safety::SealedRefined;

// Tagged<T, Source> — phantom-tag provenance / trust marker.
using ::crucible::safety::Tagged;

// Secret<T> — classified-by-default carrier.
using ::crucible::safety::Secret;
using ::crucible::safety::mint_secret;

// Monotonic<T, Cmp> — only-advance per Cmp.
using ::crucible::safety::Monotonic;

// AppendOnly<T, Storage> — grow-only container.
using ::crucible::safety::AppendOnly;

// Stale<T> — value paired with staleness semiring element τ.
using ::crucible::safety::Stale;

// TimeOrdered<T, N, Tag> — value paired with N-process vector clock.
using ::crucible::safety::TimeOrdered;

// SharedPermission<Tag> — fractional permission carrier.  The
// substrate lives in permissions/Permission.h but is re-exported
// into crucible::safety via the permissions namespace, so it is
// reachable through ::crucible::safety::SharedPermission per the
// existing fixy/Perm.h convention.
using ::crucible::safety::SharedPermission;
using ::crucible::safety::mint_permission_share;

// ─── Mutation.h derivative wrappers (5) ──────────────────────────

// WriteOnce<T> — set exactly once, then read-only.
using ::crucible::safety::WriteOnce;

// WriteOnceNonNull<Ptr> — pointer-slot one-set; nullptr-sentinel.
using ::crucible::safety::WriteOnceNonNull;

// BoundedMonotonic<T, Max, Cmp> — Monotonic + upper bound.
using ::crucible::safety::BoundedMonotonic;

// OrderedAppendOnly<T, KeyFn, Cmp, Storage> — AppendOnly + key order.
using ::crucible::safety::OrderedAppendOnly;

// AtomicMonotonic<T, Cmp> — thread-safe Monotonic over std::atomic<T>.
using ::crucible::safety::AtomicMonotonic;

}  // namespace crucible::fixy::wrap
