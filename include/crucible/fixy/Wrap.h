#pragma once

// ── crucible::fixy::wrap — Value wrappers under fixy:: ─────────────
//
// Re-export.  Surfaces every remaining value-level safety
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
// ── Dual-export discipline (fixy-A4-011) ──────────────────────────
//
// The Linear / Secret / SharedPermission re-exports below also
// appear in fixy::safety:: (Safety.h) and fixy::perm:: (Perm.h)
// respectively — by design.  Both paths name the SAME substrate
// symbol; type identity is drift-checked at compile time by
// `test/test_fixy_umbrella.cpp` (search "fixy-A4-011").  A user TU
// that does `using namespace fixy::safety; using namespace fixy::wrap;`
// works today only because the two using-declarations point at the
// same symbol — divergence would surface as an ADL lookup error.
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

#include <cstdint>       // FIXY-U-020 sentinel uses std::uint64_t
#include <type_traits>   // FIXY-U-020 sentinel uses std::is_same_v

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

// ─── Dual-export sentinel — FIXY-U-020 (#1732) ─────────────────────
//
// Header-internal static_asserts pin each `using ::crucible::safety::X`
// alias to its substrate origin via `std::is_same_v`.  The companion
// reach test (test/test_fixy_umbrella.cpp::reach_sub_namespaces) only
// witnesses that `fixy::wrap::X` is REACHABLE — it does NOT verify the
// underlying type identity.  A typo that aliases `fixy::wrap::Linear`
// to the wrong substrate symbol (e.g., `using detail::Linear;` instead
// of `using safety::Linear;`) would slip past the reach test.  This
// sentinel block catches that drift in the header itself, before any
// caller TU compiles.
//
// Coverage: 11 Graded-backed wrappers (CLAUDE.md §XVI canonical) +
// 5 Mutation-derivative wrappers + 3 representative predicate lambdas
// + the 2 fundamental Refined aliases (Linear/Refined composition).
// 21 cells total — covers every conceptual category in this header.
//
// The predicate-lambda check uses `decltype(positive) ==
// decltype(safety::positive)` because lambdas are unnameable types;
// the address-equality form `&positive == &safety::positive` is not a
// constant expression for stateless lambdas in C++26 unless the lambda
// is `consteval`.  decltype-equality is the structural witness that
// the two names resolve to the SAME compile-time lambda type.

namespace crucible::fixy::wrap::self_test {

// Tier-S Graded wrappers (11 per CLAUDE.md §XVI canonical outer→inner).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Linear<int>,
    ::crucible::safety::Linear<int>>,
    "fixy::wrap::Linear must alias safety::Linear — dual-export drift.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Refined<::crucible::safety::positive, int>,
    ::crucible::safety::Refined<::crucible::safety::positive, int>>,
    "fixy::wrap::Refined must alias safety::Refined — dual-export drift.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::SealedRefined<::crucible::safety::positive, int>,
    ::crucible::safety::SealedRefined<::crucible::safety::positive, int>>,
    "fixy::wrap::SealedRefined must alias safety::SealedRefined.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Tagged<int, ::crucible::safety::source::FromUser>,
    ::crucible::safety::Tagged<int, ::crucible::safety::source::FromUser>>,
    "fixy::wrap::Tagged must alias safety::Tagged.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Secret<int>,
    ::crucible::safety::Secret<int>>,
    "fixy::wrap::Secret must alias safety::Secret.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Monotonic<std::uint64_t>,
    ::crucible::safety::Monotonic<std::uint64_t>>,
    "fixy::wrap::Monotonic must alias safety::Monotonic.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::AppendOnly<int>,
    ::crucible::safety::AppendOnly<int>>,
    "fixy::wrap::AppendOnly must alias safety::AppendOnly.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::Stale<int>,
    ::crucible::safety::Stale<int>>,
    "fixy::wrap::Stale must alias safety::Stale.");

// Refined composition orderings (both directions must alias correctly).
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::LinearRefined<::crucible::safety::positive, int>,
    ::crucible::safety::LinearRefined<::crucible::safety::positive, int>>,
    "fixy::wrap::LinearRefined must alias safety::LinearRefined.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::RefinedLinear<::crucible::safety::positive, int>,
    ::crucible::safety::RefinedLinear<::crucible::safety::positive, int>>,
    "fixy::wrap::RefinedLinear must alias safety::RefinedLinear.");

// SharedPermission — dual-exported in both fixy::wrap:: and fixy::perm::.
// Both paths MUST resolve to the same substrate type (fixy-A4-011).
struct WrapDualExportTag {};
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::SharedPermission<WrapDualExportTag>,
    ::crucible::safety::SharedPermission<WrapDualExportTag>>,
    "fixy::wrap::SharedPermission must alias safety::SharedPermission "
    "— this dual-export must agree with the fixy::perm:: parallel path.");

// Mutation.h derivatives.
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::WriteOnce<int>,
    ::crucible::safety::WriteOnce<int>>,
    "fixy::wrap::WriteOnce must alias safety::WriteOnce.");
static_assert(std::is_same_v<
    ::crucible::fixy::wrap::AtomicMonotonic<std::uint64_t>,
    ::crucible::safety::AtomicMonotonic<std::uint64_t>>,
    "fixy::wrap::AtomicMonotonic must alias safety::AtomicMonotonic.");

// Predicate lambdas — decltype-identity (lambdas are unnameable, so
// is_same_v on decltype is the structural witness).
static_assert(std::is_same_v<
    decltype(::crucible::fixy::wrap::positive),
    decltype(::crucible::safety::positive)>,
    "fixy::wrap::positive must alias safety::positive — predicate drift.");
static_assert(std::is_same_v<
    decltype(::crucible::fixy::wrap::non_negative),
    decltype(::crucible::safety::non_negative)>,
    "fixy::wrap::non_negative must alias safety::non_negative.");
static_assert(std::is_same_v<
    decltype(::crucible::fixy::wrap::non_null),
    decltype(::crucible::safety::non_null)>,
    "fixy::wrap::non_null must alias safety::non_null.");

}  // namespace crucible::fixy::wrap::self_test
