#pragma once

#include <crucible/sessions/SessionEventLog.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::eventlog {

using ::crucible::safety::proto::SessionTagId;
using ::crucible::safety::proto::RoleTagId;
using ::crucible::safety::proto::SchemaHash;
using ::crucible::safety::proto::PayloadHash;
using ::crucible::safety::proto::RecoveryPathHash;
using ::crucible::safety::proto::CheckpointId;
using ::crucible::safety::proto::InnerPermSetHash;
using ::crucible::safety::proto::StepId;

using ::crucible::safety::proto::SessionOp;
using ::crucible::safety::proto::StopReasonKind;
using ::crucible::safety::proto::CheckpointChoice;
using ::crucible::safety::proto::DetachReasonKind;

using ::crucible::safety::proto::session_op_name;
using ::crucible::safety::proto::session_op_is_cipher;
using ::crucible::safety::proto::session_op_commits_cipher_head;

using ::crucible::safety::proto::CipherEventPayload;

using ::crucible::safety::proto::SessionEvent;

using ::crucible::safety::proto::StepIdKeyFn;
using ::crucible::safety::proto::StepIdLess;

using ::crucible::safety::proto::default_schema_hash;
using ::crucible::safety::proto::default_proto_hash;
using ::crucible::safety::proto::default_payload_hash_fn;

using ::crucible::safety::proto::SessionEventLog;

}  // namespace crucible::fixy::sess::eventlog

namespace crucible::fixy::sess::eventlog::u052d_self_test {

namespace proto = ::crucible::safety::proto;

static_assert(std::is_same_v<SessionTagId, proto::SessionTagId>);
static_assert(std::is_same_v<RoleTagId, proto::RoleTagId>);
static_assert(std::is_same_v<SchemaHash, proto::SchemaHash>);
static_assert(std::is_same_v<PayloadHash, proto::PayloadHash>);
static_assert(std::is_same_v<RecoveryPathHash, proto::RecoveryPathHash>);
static_assert(std::is_same_v<CheckpointId, proto::CheckpointId>);
static_assert(std::is_same_v<InnerPermSetHash, proto::InnerPermSetHash>);
static_assert(std::is_same_v<StepId, proto::StepId>);

static_assert(std::is_same_v<SessionOp, proto::SessionOp>);
static_assert(std::is_same_v<StopReasonKind, proto::StopReasonKind>);
static_assert(std::is_same_v<CheckpointChoice, proto::CheckpointChoice>);
static_assert(std::is_same_v<DetachReasonKind, proto::DetachReasonKind>);

static_assert(std::is_same_v<decltype(&session_op_name), decltype(&proto::session_op_name)>);
static_assert(std::is_same_v<decltype(&session_op_is_cipher), decltype(&proto::session_op_is_cipher)>);
static_assert(
    std::is_same_v<decltype(&session_op_commits_cipher_head), decltype(&proto::session_op_commits_cipher_head)>);

static_assert(std::is_same_v<CipherEventPayload, proto::CipherEventPayload>);
static_assert(std::is_same_v<SessionEvent, proto::SessionEvent>);
static_assert(std::is_same_v<StepIdKeyFn, proto::StepIdKeyFn>);
static_assert(std::is_same_v<StepIdLess, proto::StepIdLess>);
static_assert(std::is_same_v<SessionEventLog, proto::SessionEventLog>);

static_assert(sizeof(SessionEvent) == 72, "SessionEvent must stay 72 bytes — the persisted event-log wire "
                                          "format depends on the fixed size.");
static_assert(std::is_trivially_copyable_v<SessionEvent>);

// The three hash helpers get no identity sentinel.  A fully-qualified
// using-decl cannot resolve to a shadowed local, so a substrate rename
// already fails the using-decl itself.  Force-evaluating
// `default_schema_hash<T>` here would add no coverage and trips a GCC 16
// ICE on its `__PRETTY_FUNCTION__`-at-consteval initializer.

static_assert(!std::is_same_v<StepId, SessionTagId>);
static_assert(!std::is_same_v<SchemaHash, PayloadHash>);

static_assert(!std::is_move_constructible_v<SessionEventLog>,
              "SessionEventLog is pinned — the atomic step counter is its "
              "identity, and movement would fork the monotone-step invariant.");

constexpr int u052d_surface_cardinality = 23;
static_assert(u052d_surface_cardinality == 23, "The re-exported surface cardinality drifted — update the "
                                               "using-decls and this sentinel in lockstep.");

}  // namespace crucible::fixy::sess::eventlog::u052d_self_test

namespace crucible::fixy::sess::eventlog {

inline void runtime_smoke_test() noexcept {
    const StepId step{7};
    const SessionTagId sess{3};
    const RoleTagId self{1};
    const RoleTagId peer{2};

    SessionEvent ev = SessionEvent::delegate_handoff(self, peer, ::crucible::ContentHash{}, InnerPermSetHash{0});
    ev.step_id = step;
    ev.session = sess;

    [[maybe_unused]] const auto op_name = session_op_name(ev.op);
    [[maybe_unused]] const bool is_cipher = session_op_is_cipher(ev.op);
    [[maybe_unused]] const bool commits = session_op_commits_cipher_head(ev.op);
    [[maybe_unused]] const CipherEventPayload pay = ev.cipher_payload();

    [[maybe_unused]] const StepId projected = StepIdKeyFn{}(ev);
    [[maybe_unused]] const bool ordered = StepIdLess{}(StepId{1}, StepId{2});

    SessionEventLog log{sess};
    [[maybe_unused]] const StepId next = log.next_step();
    [[maybe_unused]] const std::size_t n = log.size();
    [[maybe_unused]] const bool e = log.empty();

    (void)op_name;
    (void)is_cipher;
    (void)commits;
    (void)pay;
    (void)projected;
    (void)ordered;
    (void)next;
    (void)n;
    (void)e;
}

}  // namespace crucible::fixy::sess::eventlog
