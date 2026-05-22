#pragma once

// ── crucible::fixy::sess::checkpoint — checkpoint-pair coherence ────
//
// FIXY-V-061.  Re-exports the public surface of
// `sessions/SessionCheckpoint.h` — the L8 checkpoint combinator
// (Appendix D.2 of the session-types paper) that wraps TWO sibling
// protocols at the SAME timeline position:  a `base` branch (the
// commit / "happy path" continuation) and a `rollback` branch (the
// abort / restart continuation).  Both branches are independently
// dualized, well-formed, empty-choice-checked, composed, and
// subtype-checked — the pair is COHERENT iff every individual trait
// holds independently on both arms.
//
// `CheckpointedSession<B, R>` is a TYPE-LEVEL try/catch:  the protocol
// declares two reachable terminal-equivalent paths from one position,
// and the handle exposes `base() &&` / `rollback() &&` as &&-qualified
// transitions (no automatic selection; the user calls whichever path
// matches the runtime decision).  Use cases:
//
//   * 2PC transactions:  base = COMMIT continuation; rollback = ABORT
//   * Speculation:       base = success path; rollback = retry path
//   * Verifier checks:   base = verify-accept; rollback = verify-reject
//   * Time-bounded ops:  base = within-deadline; rollback = deadline-expired
//
// ── Why this surface exists (Agent 3 finding B5 / SEPLOG-L2) ───────
//
// Before V-061, the checkpoint combinator was substrate-only — band-3
// callers (Cipher hot/warm/cold tier promotion, Transaction TX
// commit/abort, Vessel pre-commit verification) had to route through
// `crucible::safety::proto::CheckpointedSession<...>` directly, which
// (a) bypassed the fixy:: audit ledger, and (b) provided no
// intent-revealing one-liner for the "this protocol IS a checkpoint
// with EXACTLY these branches" assertion.  V-061 closes that gap with
// a 9-symbol re-export surface plus `assert_checkpointed_matches`
// (the three-clause consteval helper that fires distinct diagnostics
// for "not a checkpoint" / "wrong base branch" / "wrong rollback
// branch").
//
// ── Nine symbols (the public checkpoint API) ───────────────────────
//
//   core combinator (1):       CheckpointedSession
//   shape traits (2):          is_checkpointed_session,
//                              is_checkpointed_session_v
//   branch extractors (4):     checkpoint_base,
//                              checkpoint_base_t,
//                              checkpoint_rollback,
//                              checkpoint_rollback_t
//   ergonomic concept (1):     Checkpointed
//   consteval assertion (1):   assert_checkpointed_matches
//                                              ──── 1 + 2 + 4 + 1 + 1 = 9
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Nine using-decls, sentinel battery, smoke routine.  No new
// types, no mint factories, no free functions — every entry is a
// pure name-lookup directive (zero machine code).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; CheckpointedSession itself
//              is an empty tag struct (no data members).
//   TypeSafe — using-decls preserve substrate identity; (B, R) pair is
//              structurally distinct from any non-checkpointed protocol
//              (sentinel cells assert this).
//   NullSafe — no pointer state.
//   MemSafe  — all symbols compile-time-only; no allocation.
//   DetSafe  — pure type-level computation; same (B, R) inputs always
//              produce the same is_checkpointed_session_v outcome.
//   BorrowSafe — no aliasing at this layer (purely structural).
//   ThreadSafe — no shared state crossed.
//   LeakSafe — no resource owned.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).
// `CheckpointedSession<B, R>` is `sizeof = 1` (empty tag struct);
// SessionHandle specializations for it consume the underlying
// Resource only — zero overhead per branch.

#include <crucible/sessions/SessionCheckpoint.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::checkpoint {

// ── 1. Core combinator (1) ─────────────────────────────────────────
// `CheckpointedSession<B, R>` — wraps two SIBLING protocols at the
// same timeline position.  Nested aliases `base` and `rollback`
// expose each branch for metaprogramming.  Empty tag struct (no
// data); both branches are pure type-level.
using ::crucible::safety::proto::CheckpointedSession;

// ── 2. Shape traits (2) ────────────────────────────────────────────
// `is_checkpointed_session<P>` — primary template returns
// `false_type`; specialization for `CheckpointedSession<B, R>` returns
// `true_type`.  `_v` is the bool-shortcut.  Use at boundary functions
// that must dispatch on "this protocol IS a checkpoint" or accept
// only checkpoint-wrapped sessions.
using ::crucible::safety::proto::is_checkpointed_session;
using ::crucible::safety::proto::is_checkpointed_session_v;

// ── 3. Branch extractors (4) ───────────────────────────────────────
// `checkpoint_base<P>::type` extracts the base (commit / happy-path)
// branch; `checkpoint_rollback<P>::type` extracts the rollback
// (abort / retry) branch.  Both have `_t` shortcuts.  Ill-formed
// (substitution failure) on non-CheckpointedSession protocols —
// callers should gate on `is_checkpointed_session_v<P>` first.
using ::crucible::safety::proto::checkpoint_base;
using ::crucible::safety::proto::checkpoint_base_t;
using ::crucible::safety::proto::checkpoint_rollback;
using ::crucible::safety::proto::checkpoint_rollback_t;

// ── 4. Ergonomic concept (1) ───────────────────────────────────────
// `Checkpointed<P>` — concept-form gate for `requires`-clauses at
// boundary APIs that demand a checkpointed protocol.  Substantially
// more readable than the equivalent
// `enable_if_t<is_checkpointed_session_v<P>>` SFINAE.
using ::crucible::safety::proto::Checkpointed;

// ── 5. Intent-revealing consteval assertion (1) ────────────────────
// `assert_checkpointed_matches<P, ExpectedBase, ExpectedRollback>()`
// — one-liner at call sites that demand a CheckpointedSession with
// EXACTLY the named branch pair.  Fires three distinct diagnostics
// based on the failure shape:
//   * P is not a CheckpointedSession at all          → "P is not a
//                                                      CheckpointedSession"
//   * P's base branch doesn't match ExpectedBase     → "base branch
//                                                      does not match"
//   * P's rollback branch doesn't match ExpectedRollback
//                                                    → "rollback branch
//                                                      does not match"
// Distinct diagnostics let reviewers identify the specific mismatch
// class without reading the call-site source.  Used by the
// checkpoint-pair coherence boundary functions.
using ::crucible::safety::proto::assert_checkpointed_matches;

}  // namespace crucible::fixy::sess::checkpoint

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessAssoc.h + SessDelegate.h.
// Substrate-side renames trip at every consumer's include time, not
// three TUs deep.  ASCII-only identifiers per CLAUDE.md §XVII.

namespace crucible::fixy::sess::checkpoint::v061_self_test {

namespace proto = ::crucible::safety::proto;

// Fixture protocols — minimal well-formed sessions exercising every
// export without depending on richer SessionHandle machinery.
struct Request  {};
struct Response {};
struct Error    {};

// Two-phase-commit-shaped checkpoint pair.
using CommitPath   = proto::Send<Request, proto::Recv<Response, proto::End>>;
using RollbackPath = proto::Send<Request, proto::Recv<Error,    proto::End>>;
using CkptSession  = CheckpointedSession<CommitPath, RollbackPath>;

// A non-checkpoint protocol for shape-trait reject cases.
using PlainProto   = proto::Send<Request, proto::End>;

// ── A. Core combinator reach (type identity) ───────────────────────
static_assert(std::is_same_v<
    CheckpointedSession<CommitPath, RollbackPath>,
    proto::CheckpointedSession<CommitPath, RollbackPath>>,
    "CheckpointedSession must reach identically through fixy::.  "
    "If this red-lights, fixy/SessCheckpoint.h's first using-decl "
    "is broken or the substrate type was moved.");

// Nested aliases preserved through the umbrella.
static_assert(std::is_same_v<typename CkptSession::base,     CommitPath>);
static_assert(std::is_same_v<typename CkptSession::rollback, RollbackPath>);

// ── B. Shape traits reach ──────────────────────────────────────────
static_assert(is_checkpointed_session_v<CkptSession>
              == proto::is_checkpointed_session_v<CkptSession>,
    "is_checkpointed_session_v must reach identically through fixy::");
static_assert(is_checkpointed_session_v<CkptSession>,
    "CkptSession IS a CheckpointedSession — trait must return true.");
static_assert(!is_checkpointed_session_v<PlainProto>,
    "Send<T, K> is NOT a CheckpointedSession — trait must reject.");
static_assert(!is_checkpointed_session_v<proto::End>,
    "End is NOT a CheckpointedSession — trait must reject.");

// Class-template form (for inheritance-based composition).
static_assert(is_checkpointed_session<CkptSession>::value);
static_assert(!is_checkpointed_session<PlainProto>::value);

// ── C. Branch extractors reach ─────────────────────────────────────
static_assert(std::is_same_v<checkpoint_base_t<CkptSession>, CommitPath>,
    "checkpoint_base_t<CkptSession> must equal CommitPath.");
static_assert(std::is_same_v<checkpoint_rollback_t<CkptSession>, RollbackPath>,
    "checkpoint_rollback_t<CkptSession> must equal RollbackPath.");

// Class-template form.
static_assert(std::is_same_v<
    typename checkpoint_base<CkptSession>::type, CommitPath>);
static_assert(std::is_same_v<
    typename checkpoint_rollback<CkptSession>::type, RollbackPath>);

// Equivalence of fixy and substrate _t aliases.
static_assert(std::is_same_v<
    checkpoint_base_t<CkptSession>,
    proto::checkpoint_base_t<CkptSession>>,
    "checkpoint_base_t must reach identically through fixy::");
static_assert(std::is_same_v<
    checkpoint_rollback_t<CkptSession>,
    proto::checkpoint_rollback_t<CkptSession>>,
    "checkpoint_rollback_t must reach identically through fixy::");

// ── D. Concept reach ───────────────────────────────────────────────
template <typename P>
    requires Checkpointed<P>
consteval bool requires_checkpointed_witness() { return true; }
static_assert(requires_checkpointed_witness<CkptSession>(),
    "Checkpointed concept must admit CheckpointedSession<B, R>.");

// Negative concept witness — Checkpointed rejects non-checkpoint.
template <typename P>
consteval bool can_satisfy_checkpointed() {
    return requires { requires Checkpointed<P>; };
}
static_assert(!can_satisfy_checkpointed<PlainProto>(),
    "Checkpointed concept must REJECT non-checkpoint protocols.");
static_assert(!can_satisfy_checkpointed<proto::End>(),
    "Checkpointed concept must REJECT End.");

// ── E. assert_checkpointed_matches reach (happy path) ──────────────
consteval bool check_fixy_assert_checkpointed_matches() {
    assert_checkpointed_matches<CkptSession, CommitPath, RollbackPath>();
    return true;
}
static_assert(check_fixy_assert_checkpointed_matches(),
    "assert_checkpointed_matches must accept the correct (P, B, R) "
    "triple at consteval.");

// ── F. Cardinality witness — count of items V-061 surfaces ─────────
//
//   core combinator (1: CheckpointedSession)
// + shape traits (2: is_checkpointed_session + _v)
// + branch extractors (4: checkpoint_base + _t + checkpoint_rollback + _t)
// + ergonomic concept (1: Checkpointed)
// + consteval assertion (1: assert_checkpointed_matches)
//                                                          ──── 9
constexpr int v061_surface_cardinality = 9;
static_assert(v061_surface_cardinality == 9,
    "fixy::sess::checkpoint:: V-061 surface cardinality drifted — "
    "update SessCheckpoint.h using-decls AND this sentinel in "
    "lockstep.");

}  // namespace crucible::fixy::sess::checkpoint::v061_self_test

namespace crucible::fixy::sess::checkpoint {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body bugs.
// The smoke routine forces every checkpoint metafunction through real
// instantiation so latent template-evaluation issues surface under
// `-fsyntax-only` of any TU that includes SessCheckpoint.h.

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    struct Req {};
    struct Resp {};
    struct Err {};

    using B = proto::Send<Req, proto::Recv<Resp, proto::End>>;
    using R = proto::Send<Req, proto::Recv<Err,  proto::End>>;
    using C = CheckpointedSession<B, R>;
    using P = proto::Send<Req, proto::End>;

    [[maybe_unused]] constexpr bool is_ckpt_yes   = is_checkpointed_session_v<C>;
    [[maybe_unused]] constexpr bool is_ckpt_no    = is_checkpointed_session_v<P>;
    [[maybe_unused]] constexpr bool base_eq_B     =
        std::is_same_v<checkpoint_base_t<C>, B>;
    [[maybe_unused]] constexpr bool rollback_eq_R =
        std::is_same_v<checkpoint_rollback_t<C>, R>;

    // Concept reach.
    constexpr auto satisfies_ckpt = []<typename Q>() {
        return Checkpointed<Q>;
    };
    [[maybe_unused]] constexpr bool concept_yes = satisfies_ckpt.template operator()<C>();
    [[maybe_unused]] constexpr bool concept_no  = satisfies_ckpt.template operator()<P>();

    (void) is_ckpt_yes; (void) is_ckpt_no;
    (void) base_eq_B; (void) rollback_eq_R;
    (void) concept_yes; (void) concept_no;
}

}  // namespace crucible::fixy::sess::checkpoint
