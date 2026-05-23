#pragma once

// ── crucible::fixy::spawn::grant — engagement-grant tree ───────────
//
// FIXY-V-204 (Agent 7 §fixy::spawn::).  Five rationale-bearing /
// type-parameterized grants that audit-trail every non-default spawn
// engagement at the type level.  Each grant inherits `grant_base`,
// gets a `which_dim<>` specialization routing it to
// `DimensionAxis::Protocol` (spawn engagement IS a parent-child
// protocol — same axis as binary-session protocol grants), and the
// three Rationale-bearing grants enforce `rationale_nonempty_v<R>`
// via in-class `static_assert`.
//
// ── The five grants ────────────────────────────────────────────────
//
//     detach_with<Rationale>  Audit trail for `jthread.detach()` —
//                             paired with V-203 `join::Detached` at
//                             the spawn site.  Without `detach_with`
//                             the mint refuses the Detached mechanism.
//     syscall_only<Rationale> Audit trail for raw Linux `clone(2)` —
//                             paired with V-203 `join::Cloned`.
//                             Allowed only in perf/bpf/cog loaders
//                             that genuinely need CLONE_VM /
//                             CLONE_THREAD / CLONE_FILES semantics.
//     subprocess<Rationale>   Audit trail for `fork(2)` / `posix_spawn(3)` —
//                             paired with V-203 `join::Forked` /
//                             `join::PosixSpawn`.  Composes with the
//                             V-210 CRUCIBLE_SPAWN_ALLOW_PROCESS
//                             CMake opt-in (script-side guard at
//                             scripts/check-fixy-spawn-discipline.sh).
//     fork_parent<ParentTag>  Threads the parent Permission tag
//                             through the grant set — lets the
//                             coherence concept consume the parent
//                             identity without retyping it at the
//                             mint_spawn call site.
//     exec_ctx<Ctx>           Threads the ExecCtx through the grant
//                             set — mirrors V-217 CtxAdmitsCap as a
//                             named type-level witness.
//
// ── Why Rationale is a string NTTP, not a free type parameter ──────
//
// P3491R3 fixed-string non-type template parameters let `detach_with<
// "logger drain outlives container">` deduce a `rationale<N>` (the
// V-244 NTTP type re-used from fixy/grant/Ctrl.h) directly from the
// bare literal.  Two consequences:
//
//   (1) Audit trail in the type.  Two `detach_with` sites with
//       different strings produce DISTINCT types (distinct
//       federation-cache slots).  `grep "detach_with<"` enumerates
//       every detach justification in the codebase.
//   (2) `rationale_nonempty_v<Reason>` is a single integer-compare
//       (`Reason.size() > 1` — N includes the trailing NUL).  Each
//       grant ships an in-class `static_assert(rationale_nonempty_v<
//       Rationale>)` so `grant::detach_with<"">` hard-errors at
//       instantiation, BEFORE any consumer sees it — same shape as
//       the secret_policy::* declassification policy discipline in
//       safety/Secret.h.
//
// ── JoinPolicyGrantsCoherent — the V-203×V-204 bridge ──────────────
//
// The coherence concept gates a grant pack against a chosen V-203
// join mechanism:
//
//   - AutoJoin    → any grant set (incl. empty)
//   - ManualJoin  → any grant set (the join is acknowledged via the
//                   tag itself; no additional grant needed)
//   - Detached    → MUST contain a `detach_with<>` grant
//   - Cloned      → MUST contain a `syscall_only<>` grant
//   - Forked      → MUST contain a `subprocess<>` grant
//   - PosixSpawn  → MUST contain a `subprocess<>` grant
//
// A future mint_spawn variant will fold this concept into its
// requires-clause; V-204 ships the concept STANDALONE so the
// substrate is testable today and downstream wiring is a thin
// `requires JoinPolicyGrantsCoherent<...>` injection.
//
// ── §XXI compliance ────────────────────────────────────────────────
//
// V-204 ships NO new `mint_*` factory — the grants are passive type-
// level identifiers; their soundness gate is in-class
// `static_assert(rationale_nonempty_v<Rationale>)` (fires at grant
// instantiation, BEFORE the grant reaches any consumer).  The
// JoinPolicyGrantsCoherent concept is a passive predicate, not a
// factory.
//
// ── HS14 floor ─────────────────────────────────────────────────────
//
// Three fixtures cover orthogonal rejection axes:
//
//   1. test/fixy_neg/neg_fixy_v_204_grant_empty_rationale.cpp
//        — `grant::detach_with<"">` reds via in-class
//          static_assert(rationale_nonempty_v<>); empty NTTP rejected
//          BEFORE any consumer sees the grant.
//   2. test/fixy_neg/neg_fixy_v_204_grant_detached_missing_detach_with.cpp
//        — `JoinPolicyGrantsCoherent<join::Detached, ...>` (empty
//          grant pack) reds; the coherence concept fires on missing
//          detach_with for Detached mechanism.
//   3. test/fixy_neg/neg_fixy_v_204_grant_forked_missing_subprocess.cpp
//        — `JoinPolicyGrantsCoherent<join::Forked, ...>` with only
//          a stray grant (no `subprocess<>`) reds; mirrors fixture #2
//          but on the Forked/PosixSpawn axis (distinct mismatch
//          class: process-spawn vs detach-spawn).
//
// ── Sentinel TU ───────────────────────────────────────────────────
//
// test/test_fixy_v_204_spawn_grant.cpp forces every header-embedded
// static_assert under project warning flags (per
// feedback_header_only_static_assert_blind_spot).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — empty structs; no state.
//   TypeSafe   — Rationale is rationale<N> (strong-typed NTTP);
//                ParentTag/Ctx are template type parameters with
//                no implicit conversion.
//   NullSafe   — no pointers.
//   MemSafe    — phantom tags carry no resources.
//   BorrowSafe — grants are not transferred between threads; type-
//                level identifiers, not values.
//   ThreadSafe — same.
//   LeakSafe   — same.
//   DetSafe    — every operation is `consteval` or `constexpr`; no
//                runtime work.

#include <crucible/Platform.h>
#include <crucible/fixy/Dim.h>                 // dim::DimensionAxis
#include <crucible/fixy/Grant.h>               // grant_base + which_dim
#include <crucible/fixy/grant/Ctrl.h>          // ctrl::rationale<N>
#include <crucible/fixy/spawn/JoinPolicy.h>    // V-203 mechanism tags

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::spawn::grant {

// ── Re-export rationale<N> for grep-discoverability ────────────────
//
// Callers write `spawn::grant::detach_with<"reason">` without needing
// to know about `fixy/grant/Ctrl.h`.  Same NTTP type — distinct
// strings still produce distinct types (load-bearing for federation-
// cache discrimination).
template <std::size_t N>
using rationale = ::crucible::fixy::grant::ctrl::rationale<N>;

// ── rationale_nonempty_v<R> — P3491R3 NTTP empty-string check ─────
//
// N includes the trailing NUL, so `rationale<1>{""}` has size == 1
// and is rejected; `rationale<2>{"x"}` has size == 2 and accepted.
// Mirrors the existing fixy::hw::rationale_nonempty_v at fixy/Hw.h:230.
template <::crucible::fixy::grant::ctrl::rationale Reason>
inline constexpr bool rationale_nonempty_v = (Reason.size() > 1);

// ═══════════════════════════════════════════════════════════════════
// ── The five engagement grants ─────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════

// (1) detach_with<Rationale> — pairs with V-203 join::Detached.
//
// Rationale is MANDATORY: no default template argument.  In-class
// static_assert rejects empty literals.
template <::crucible::fixy::grant::ctrl::rationale Rationale>
struct detach_with final : ::crucible::fixy::grant::grant_base {
    static_assert(rationale_nonempty_v<Rationale>,
        "grant::detach_with<\"\"> rejected — Rationale must be "
        "non-empty.  Every detach() audit-trail must declare its "
        "non-engagement reason in the type so `grep \"detach_with<\"`"
        " enumerates every legitimate detach across the codebase.  "
        "Replace the empty literal with a descriptive justification "
        "(e.g., \"logger drain outlives container\").");
    static constexpr ::crucible::fixy::grant::ctrl::rationale reason = Rationale;
};

// (2) syscall_only<Rationale> — pairs with V-203 join::Cloned.
template <::crucible::fixy::grant::ctrl::rationale Rationale>
struct syscall_only final : ::crucible::fixy::grant::grant_base {
    static_assert(rationale_nonempty_v<Rationale>,
        "grant::syscall_only<\"\"> rejected — Rationale must be "
        "non-empty.  Raw clone(2) bypasses libc thread machinery "
        "and is reserved for perf/bpf/cog loaders that genuinely "
        "need CLONE_VM / CLONE_THREAD / CLONE_FILES semantics; the "
        "audit trail of which loader and why is load-bearing.");
    static constexpr ::crucible::fixy::grant::ctrl::rationale reason = Rationale;
};

// (3) subprocess<Rationale> — pairs with V-203 join::Forked /
// join::PosixSpawn.
template <::crucible::fixy::grant::ctrl::rationale Rationale>
struct subprocess final : ::crucible::fixy::grant::grant_base {
    static_assert(rationale_nonempty_v<Rationale>,
        "grant::subprocess<\"\"> rejected — Rationale must be "
        "non-empty.  fork(2) / posix_spawn(3) creates a separate "
        "process image that escapes the entire fixy:: type system; "
        "the script-side opt-in (V-210 CRUCIBLE_SPAWN_ALLOW_PROCESS) "
        "guards the call site, this in-type grant complements it "
        "with the audit-trail rationale.");
    static constexpr ::crucible::fixy::grant::ctrl::rationale reason = Rationale;
};

// (4) fork_parent<ParentTag> — threads the parent Permission tag
// through the grant set.  Lets the coherence concept consume the
// parent identity at the grant level rather than reading it off the
// mint_spawn template-parameter list.
template <typename ParentTag>
struct fork_parent final : ::crucible::fixy::grant::grant_base {
    using parent_tag = ParentTag;
};

// (5) exec_ctx<Ctx> — threads the ExecCtx through the grant set.
// Mirrors V-217 CtxAdmitsCap as a named type-level witness.
template <typename Ctx>
struct exec_ctx final : ::crucible::fixy::grant::grant_base {
    using ctx_type = Ctx;
};

// ═══════════════════════════════════════════════════════════════════
// ── Grant detection helpers ────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════
//
// The coherence concept needs to ask "does the grant pack contain a
// detach_with<...> for any Rationale?" — that's a class-template
// matching question.  Standard pattern: a per-grant `is_*` trait
// that primary-template-false-types and specialization-true-types
// against the template instantiation.

namespace detail {

template <typename G> struct is_detach_with  : std::false_type {};
template <::crucible::fixy::grant::ctrl::rationale R>
struct is_detach_with<detach_with<R>>        : std::true_type {};

template <typename G> struct is_syscall_only : std::false_type {};
template <::crucible::fixy::grant::ctrl::rationale R>
struct is_syscall_only<syscall_only<R>>      : std::true_type {};

template <typename G> struct is_subprocess   : std::false_type {};
template <::crucible::fixy::grant::ctrl::rationale R>
struct is_subprocess<subprocess<R>>          : std::true_type {};

// any_of_v<Pred, Grants...> — fold across a parameter pack to test
// whether any grant satisfies the per-grant `is_*` predicate.
template <template <typename> class Pred, typename... Grants>
inline constexpr bool any_of_v = (Pred<Grants>::value || ...);

}  // namespace detail

// ── Per-family probe — public-facing predicates ────────────────────
//
// Three single-purpose variable templates the coherence concept (and
// downstream consumers) use to ask "does this grant pack contain a
// X<>?".  Simpler than a generic template-template parameter
// dispatcher and produces sharper diagnostics on mismatch.
template <typename... Grants>
inline constexpr bool has_detach_with_v =
    detail::any_of_v<detail::is_detach_with, Grants...>;

template <typename... Grants>
inline constexpr bool has_syscall_only_v =
    detail::any_of_v<detail::is_syscall_only, Grants...>;

template <typename... Grants>
inline constexpr bool has_subprocess_v =
    detail::any_of_v<detail::is_subprocess, Grants...>;

}  // namespace crucible::fixy::spawn::grant

// ═══════════════════════════════════════════════════════════════════
// ── which_dim routing — CR-09: lives in `namespace fixy::grant` ────
// ═══════════════════════════════════════════════════════════════════

namespace crucible::fixy::grant {

template <::crucible::fixy::grant::ctrl::rationale R>
struct which_dim<::crucible::fixy::spawn::grant::detach_with<R>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <::crucible::fixy::grant::ctrl::rationale R>
struct which_dim<::crucible::fixy::spawn::grant::syscall_only<R>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <::crucible::fixy::grant::ctrl::rationale R>
struct which_dim<::crucible::fixy::spawn::grant::subprocess<R>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <typename ParentTag>
struct which_dim<::crucible::fixy::spawn::grant::fork_parent<ParentTag>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

template <typename Ctx>
struct which_dim<::crucible::fixy::spawn::grant::exec_ctx<Ctx>>
    : std::integral_constant<dim::DimensionAxis, dim::DimensionAxis::Protocol> {};

}  // namespace crucible::fixy::grant

// ═══════════════════════════════════════════════════════════════════
// ── JoinPolicyGrantsCoherent — the V-203×V-204 bridge ──────────────
// ═══════════════════════════════════════════════════════════════════

namespace crucible::fixy::spawn {

namespace join = ::crucible::fixy::spawn::join;

// True iff the grant pack contains EXACTLY the grant family the
// mechanism's V-203 surface requires.  AutoJoin and ManualJoin are
// the default-acceptable mechanisms (no audit-trail grant required);
// Detached / Cloned / Forked / PosixSpawn each demand a specific
// grant family witness in the pack.
//
// Concept name spells the invariant: "the V-203 mechanism and the
// V-204 grant set are mutually coherent."
template <typename Mechanism, typename... Grants>
concept JoinPolicyGrantsCoherent =
    join::IsJoinMechanismTag<Mechanism>
    && (
        std::is_same_v<Mechanism, join::AutoJoin>     // default — no grant required
     || std::is_same_v<Mechanism, join::ManualJoin>   // tag-acknowledged join site
     || (std::is_same_v<Mechanism, join::Detached>
         && grant::detail::any_of_v<grant::detail::is_detach_with, Grants...>)
     || (std::is_same_v<Mechanism, join::Cloned>
         && grant::detail::any_of_v<grant::detail::is_syscall_only, Grants...>)
     || (std::is_same_v<Mechanism, join::Forked>
         && grant::detail::any_of_v<grant::detail::is_subprocess, Grants...>)
     || (std::is_same_v<Mechanism, join::PosixSpawn>
         && grant::detail::any_of_v<grant::detail::is_subprocess, Grants...>)
    );

}  // namespace crucible::fixy::spawn

// ═══════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ───────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════
namespace crucible::fixy::spawn::grant::detail::v204_self_test {

namespace dim  = ::crucible::fixy::dim;
namespace join = ::crucible::fixy::spawn::join;
namespace fg   = ::crucible::fixy::grant;
namespace ctrl = ::crucible::fixy::grant::ctrl;

// ── rationale_nonempty_v witness ───────────────────────────────────
static_assert( rationale_nonempty_v<ctrl::rationale{"x"}>);
static_assert( rationale_nonempty_v<ctrl::rationale{"reason"}>);
static_assert(!rationale_nonempty_v<ctrl::rationale{""}>);

// ── Grants inherit grant_base (recognized by the fixy::grant substrate) ──
static_assert(std::is_base_of_v<fg::grant_base, detach_with<ctrl::rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, syscall_only<ctrl::rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, subprocess<ctrl::rationale{"x"}>>);
static_assert(std::is_base_of_v<fg::grant_base, fork_parent<struct dummy_parent_tag>>);
static_assert(std::is_base_of_v<fg::grant_base, exec_ctx<struct dummy_ctx>>);

// ── which_dim routing ──────────────────────────────────────────────
static_assert(fg::which_dim_v<detach_with<ctrl::rationale{"x"}>>     == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<syscall_only<ctrl::rationale{"x"}>>    == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<subprocess<ctrl::rationale{"x"}>>      == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<fork_parent<struct dummy_parent_tag2>> == dim::DimensionAxis::Protocol);
static_assert(fg::which_dim_v<exec_ctx<struct dummy_ctx2>>           == dim::DimensionAxis::Protocol);

// ── EBO size: each grant is empty (sizeof == 1, EBO-collapsible) ──
static_assert(std::is_empty_v<detach_with<ctrl::rationale{"x"}>>);
static_assert(std::is_empty_v<syscall_only<ctrl::rationale{"x"}>>);
static_assert(std::is_empty_v<subprocess<ctrl::rationale{"x"}>>);
static_assert(std::is_empty_v<fork_parent<struct dummy3>>);
static_assert(std::is_empty_v<exec_ctx<struct dummy4>>);
static_assert(sizeof(detach_with<ctrl::rationale{"x"}>) == 1);

// ── Reason field accessible at compile time ────────────────────────
static_assert(detach_with<ctrl::rationale{"audit"}>::reason.size() == 6);  // "audit" + NUL

// ── Distinct rationales → distinct types ───────────────────────────
static_assert(!std::is_same_v<
    detach_with<ctrl::rationale{"reason_a"}>,
    detach_with<ctrl::rationale{"reason_b"}>>);

// ── Detection helpers ──────────────────────────────────────────────
static_assert( any_of_v<is_detach_with,  detach_with<ctrl::rationale{"x"}>>);
static_assert(!any_of_v<is_detach_with,  syscall_only<ctrl::rationale{"x"}>>);
static_assert( any_of_v<is_detach_with,
    syscall_only<ctrl::rationale{"x"}>,
    detach_with<ctrl::rationale{"y"}>>);  // anywhere in pack
static_assert(!any_of_v<is_detach_with>); // empty pack

// ── JoinPolicyGrantsCoherent — full per-mechanism truth table ─────
//
// AutoJoin / ManualJoin: trivially coherent with any grant set,
// including empty.
static_assert( ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::AutoJoin>);
static_assert( ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::ManualJoin>);
static_assert( ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::AutoJoin,
    detach_with<ctrl::rationale{"unused but allowed"}>>);

// Detached: needs detach_with<>.
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached>);
static_assert( ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached,
    detach_with<ctrl::rationale{"logger drain outlives container"}>>);
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached,
    subprocess<ctrl::rationale{"wrong grant family"}>>);

// Cloned: needs syscall_only<>.
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Cloned>);
static_assert( ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Cloned,
    syscall_only<ctrl::rationale{"perf bpf loader needs CLONE_VM"}>>);

// Forked: needs subprocess<>.
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Forked>);
static_assert( ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Forked,
    subprocess<ctrl::rationale{"CLI launcher fork-then-exec"}>>);

// PosixSpawn: same subprocess<> family.
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::PosixSpawn>);
static_assert( ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::PosixSpawn,
    subprocess<ctrl::rationale{"test-harness fork-exec helper"}>>);

// Cross-family negatives — verify the gate doesn't accidentally pass
// the wrong family for the mechanism.
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached,
    syscall_only<ctrl::rationale{"wrong family"}>>);
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Cloned,
    detach_with<ctrl::rationale{"wrong family"}>>);
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Forked,
    detach_with<ctrl::rationale{"wrong family"}>>);

// Mixed grant pack with the RIGHT family alongside others passes.
static_assert( ::crucible::fixy::spawn::JoinPolicyGrantsCoherent<join::Detached,
    fork_parent<struct dummy_pt>,
    detach_with<ctrl::rationale{"r"}>,
    exec_ctx<struct dummy_cx>>);

// Non-mechanism first arg fails the concept (IsJoinMechanismTag gate).
static_assert(!::crucible::fixy::spawn::JoinPolicyGrantsCoherent<int>);

}  // namespace crucible::fixy::spawn::grant::detail::v204_self_test
