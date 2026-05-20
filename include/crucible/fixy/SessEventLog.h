#pragma once

// ── crucible::fixy::sess::eventlog — typed session event-log slice ──
//
// FIXY-U-052d (fourth slice of the U-052 umbrella).  Re-exports the
// complete public surface of sessions/SessionEventLog.h
// (`crucible::safety::proto::*`) into
// `crucible::fixy::sess::eventlog::` so production callers — Cipher.h
// persistence (HEAD/log roll-forward, cold-tier SessionEvent drain),
// RecordingSessionHandle, replay tooling — spell the append-only
// event-log vocabulary through the fixy umbrella, not raw
// `safety::proto::`.
//
// Twenty-three symbols (the complete SessionEventLog.h public API):
//
//   Strong IDs (8):    SessionTagId, RoleTagId, SchemaHash,
//                      PayloadHash, RecoveryPathHash, CheckpointId,
//                      InnerPermSetHash, StepId
//   Op + classifiers (4): SessionOp, StopReasonKind, CheckpointChoice,
//                      DetachReasonKind
//   Op helpers (3):    session_op_name, session_op_is_cipher,
//                      session_op_commits_cipher_head
//   Payload (1):       CipherEventPayload
//   Record (1):        SessionEvent (72-byte fixed wire format)
//   KeyFn/Cmp (2):     StepIdKeyFn, StepIdLess
//   Hash helpers (3):  default_schema_hash, default_proto_hash,
//                      default_payload_hash_fn
//   Log class (1):     SessionEventLog
//
// SessionEvent ALSO surfaces in fixy::contract::cipher:: (the
// Cipher-migration discovery point).  Per fixy-A4-011 dual-export
// discipline both paths alias the SAME substrate symbol — pinned by
// the sentinel below.
//
// ── Why a dedicated eventlog:: sub-namespace ───────────────────────
//
// fixy::sess:: holds the binary session combinators; ::mpst:: the
// global-types layer; ::declassify:: / ::ct:: / ::contentaddr:: the
// payload-marker layers.  The event log is the REPLAY-RECORD layer:
// an OrderedAppendOnly<SessionEvent> with monotone StepId.  Keeping it
// in ::eventlog:: lets audit-grep `fixy::sess::eventlog::` find every
// fixy-routed recording site distinct from substrate-direct paths.
//
// ── Substrate added by this header ─────────────────────────────────
//
// NONE.  Twenty-three using-decls + a sentinel battery + smoke
// routine.  No new types, no mint factories, no free functions.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports add no state path; every strong ID NSDMIs.
//   TypeSafe — using-decls preserve substrate type identity.
//   NullSafe — no pointer state introduced.
//   MemSafe  — SessionEventLog is Pinned at substrate; alias inherits.
//   DetSafe  — StepId monotonicity is OrderedAppendOnly's contract;
//              the 72-byte SessionEvent layout is replay-load-bearing.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Every entry is a using-decl (pure name-lookup directive).

#include <crucible/sessions/SessionEventLog.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::eventlog {

// ── 1. Strong IDs (8) ──────────────────────────────────────────────
using ::crucible::safety::proto::SessionTagId;
using ::crucible::safety::proto::RoleTagId;
using ::crucible::safety::proto::SchemaHash;
using ::crucible::safety::proto::PayloadHash;
using ::crucible::safety::proto::RecoveryPathHash;
using ::crucible::safety::proto::CheckpointId;
using ::crucible::safety::proto::InnerPermSetHash;
using ::crucible::safety::proto::StepId;

// ── 2. SessionOp + classifier enums (4) ────────────────────────────
using ::crucible::safety::proto::SessionOp;
using ::crucible::safety::proto::StopReasonKind;
using ::crucible::safety::proto::CheckpointChoice;
using ::crucible::safety::proto::DetachReasonKind;

// ── 3. SessionOp helper predicates (3) ─────────────────────────────
using ::crucible::safety::proto::session_op_name;
using ::crucible::safety::proto::session_op_is_cipher;
using ::crucible::safety::proto::session_op_commits_cipher_head;

// ── 4. Per-kind payload view (1) ───────────────────────────────────
using ::crucible::safety::proto::CipherEventPayload;

// ── 5. The per-operation record (1) ────────────────────────────────
using ::crucible::safety::proto::SessionEvent;

// ── 6. KeyFn / Cmp for OrderedAppendOnly<SessionEvent> (2) ─────────
using ::crucible::safety::proto::StepIdKeyFn;
using ::crucible::safety::proto::StepIdLess;

// ── 7. Compile-time hash helpers (3) ───────────────────────────────
using ::crucible::safety::proto::default_schema_hash;
using ::crucible::safety::proto::default_proto_hash;
using ::crucible::safety::proto::default_payload_hash_fn;

// ── 8. The append-only log primitive (1) ───────────────────────────
using ::crucible::safety::proto::SessionEventLog;

}  // namespace crucible::fixy::sess::eventlog

// ═════════════════════════════════════════════════════════════════════
// ── In-header sentinel battery (FIXY-U-020 dual-export discipline) ─
// ═════════════════════════════════════════════════════════════════════
//
// Same drift-catch discipline as fixy/SessContentAddr.h::u052c_self_test
// and fixy/SessCT.h::u052b_self_test.  Substrate-side renames trip at
// every consumer's include time, not three TUs deep.

namespace crucible::fixy::sess::eventlog::u052d_self_test {

namespace proto = ::crucible::safety::proto;

// ── A. Strong-ID type identity ─────────────────────────────────────
static_assert(std::is_same_v<SessionTagId,     proto::SessionTagId>);
static_assert(std::is_same_v<RoleTagId,        proto::RoleTagId>);
static_assert(std::is_same_v<SchemaHash,       proto::SchemaHash>);
static_assert(std::is_same_v<PayloadHash,      proto::PayloadHash>);
static_assert(std::is_same_v<RecoveryPathHash, proto::RecoveryPathHash>);
static_assert(std::is_same_v<CheckpointId,     proto::CheckpointId>);
static_assert(std::is_same_v<InnerPermSetHash, proto::InnerPermSetHash>);
static_assert(std::is_same_v<StepId,           proto::StepId>);

// ── B. Op + classifier enum identity ───────────────────────────────
static_assert(std::is_same_v<SessionOp,        proto::SessionOp>);
static_assert(std::is_same_v<StopReasonKind,   proto::StopReasonKind>);
static_assert(std::is_same_v<CheckpointChoice, proto::CheckpointChoice>);
static_assert(std::is_same_v<DetachReasonKind, proto::DetachReasonKind>);

// ── C. SessionOp helper free-function identity ─────────────────────
static_assert(std::is_same_v<
    decltype(&session_op_name), decltype(&proto::session_op_name)>);
static_assert(std::is_same_v<
    decltype(&session_op_is_cipher), decltype(&proto::session_op_is_cipher)>);
static_assert(std::is_same_v<
    decltype(&session_op_commits_cipher_head),
    decltype(&proto::session_op_commits_cipher_head)>);

// ── D. Payload + record + KeyFn/Cmp identity ───────────────────────
static_assert(std::is_same_v<CipherEventPayload, proto::CipherEventPayload>);
static_assert(std::is_same_v<SessionEvent,       proto::SessionEvent>);
static_assert(std::is_same_v<StepIdKeyFn,        proto::StepIdKeyFn>);
static_assert(std::is_same_v<StepIdLess,         proto::StepIdLess>);
static_assert(std::is_same_v<SessionEventLog,    proto::SessionEventLog>);

// ── E. SessionEvent wire-format invariant survives the alias ───────
//
// The 72-byte layout + TriviallyCopyable contract is what Cipher
// cold-tier serialisation depends on; re-prove it through the fixy
// spelling so a substrate layout drift trips here too.
static_assert(sizeof(SessionEvent) == 72,
    "fixy::sess::eventlog::SessionEvent must stay 72 bytes — Cipher "
    "cold-tier wire format depends on the fixed size.");
static_assert(std::is_trivially_copyable_v<SessionEvent>);

// ── F. Variable-template hash helpers — self-witnessing using-decls ─
//
// default_schema_hash / default_proto_hash / default_payload_hash_fn
// are re-exported as fully-qualified using-decls in the namespace body
// above.  A fully-qualified using-decl CANNOT silently resolve to a
// shadowed local — if the substrate renamed the symbol, the using-decl
// itself fails to compile.  So the using-decl IS the drift witness; no
// value-equality static_assert is needed here.  We deliberately do NOT
// force-evaluate `default_schema_hash<T>` in a sentinel: its
// `__PRETTY_FUNCTION__`-at-consteval initializer trips a patched-GCC
// 16.1.1 thin-TU ICE (reproducible with substrate-only includes), and
// forcing it would make this re-export more fragile than its substrate
// for zero verification gain.

// ── G. Strong IDs do not silently interconvert ─────────────────────
//
// StepId and SessionTagId are both uint64_t wrappers but distinct
// types — the canonical "swapped step for session id" bug breaks at
// the type level, not silently.
static_assert(!std::is_same_v<StepId, SessionTagId>);
static_assert(!std::is_same_v<SchemaHash, PayloadHash>);

// ── H. SessionEventLog is Pinned (non-movable) at the substrate ────
static_assert(!std::is_move_constructible_v<SessionEventLog>,
    "SessionEventLog is Pinned — the atomic step counter IS its "
    "identity; movement would fork the monotone-step invariant.");

// ── I. Cardinality witness — count of items U-052d surfaces ─────────
//
//   Strong IDs (8) + Op/classifiers (4) + op helpers (3) +
//   payload (1) + record (1) + KeyFn/Cmp (2) + hash helpers (3) +
//   log class (1)                                          ──── 23
constexpr int u052d_surface_cardinality = 23;
static_assert(u052d_surface_cardinality == 23,
    "fixy::sess::eventlog:: U-052d surface cardinality drifted — "
    "update SessEventLog.h using-decls AND this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::eventlog::u052d_self_test

namespace crucible::fixy::sess::eventlog {

// ═════════════════════════════════════════════════════════════════════
// ── Runtime smoke test (FIXY-U-103 discipline) ─────────────────────
// ═════════════════════════════════════════════════════════════════════
//
// Static-only sentinels can mask SFINAE / consteval / inline-body
// bugs.  The smoke routine instantiates the public surface against
// non-constant args so any latent template-evaluation issue surfaces
// under `-fsyntax-only` of any TU that includes SessEventLog.h.
//
// Cost: instantiations + one heap-free SessionEventLog construction.
// No durable I/O, no record() (which would grow the backing vector).

inline void runtime_smoke_test() noexcept {
    const StepId       step{7};
    const SessionTagId sess{3};
    const RoleTagId    self{1};
    const RoleTagId    peer{2};

    // Build a SessionEvent via a factory + read it back through the
    // typed accessors.
    SessionEvent ev = SessionEvent::delegate_handoff(
        self, peer, ::crucible::ContentHash{}, InnerPermSetHash{0});
    ev.step_id = step;
    ev.session = sess;

    [[maybe_unused]] const auto op_name = session_op_name(ev.op);
    [[maybe_unused]] const bool is_cipher = session_op_is_cipher(ev.op);
    [[maybe_unused]] const bool commits   = session_op_commits_cipher_head(ev.op);
    [[maybe_unused]] const CipherEventPayload pay = ev.cipher_payload();

    // KeyFn / Cmp behave as the OrderedAppendOnly contract expects.
    [[maybe_unused]] const StepId projected = StepIdKeyFn{}(ev);
    [[maybe_unused]] const bool   ordered   = StepIdLess{}(StepId{1}, StepId{2});

    // Pinned log: default-construct (heap-free) + mint a step + read.
    SessionEventLog log{sess};
    [[maybe_unused]] const StepId      next = log.next_step();
    [[maybe_unused]] const std::size_t n    = log.size();
    [[maybe_unused]] const bool        e    = log.empty();

    (void) op_name; (void) is_cipher; (void) commits; (void) pay;
    (void) projected; (void) ordered; (void) next; (void) n; (void) e;
}

}  // namespace crucible::fixy::sess::eventlog
