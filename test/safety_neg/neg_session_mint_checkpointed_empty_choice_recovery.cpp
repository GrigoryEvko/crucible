// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-024 mint-composition witness #2.  Demonstrates that
// `mint_session_handle<CheckpointedSession<B, R>>(resource)` fires
// the `is_empty_choice_v` static_assert when R contains a reachable
// empty `Select<>` — even though B is perfectly well-formed and
// runnable on its own.
//
// The composition story: `is_empty_choice<CheckpointedSession<B, R>>`
// (SessionCheckpoint.h, fixy-A2-004) distributes DISJUNCTIVELY over
// B and R (`is_empty_choice<B>::value || is_empty_choice<R>::value`),
// so an empty Choice in EITHER arm poisons the whole composite.  At
// the mint site (Session.h:2440-2453), this triggers the
// `static_assert(!is_empty_choice_v<Proto>, [Empty_Choice_Combinator])`
// rejection.
//
// This fixture is the EMPTY-CHOICE companion to
// neg_session_mint_checkpointed_ill_formed_base.cpp (fixy-A2-024
// mint-composition witness #1, the WELL-FORMED rejection).
// Together they pin the two-gate composition documented in
// SessionCheckpoint.h's fixy-A2-024 doc-block: an unwary user who
// constructs a Checkpoint with a stub empty-Select recovery branch
// gets a clean diagnostic at mint time, not at the eventual
// `.pick<I>()` runtime dead-end.
//
// Expected diagnostic family (one or more should match):
//   "static assertion failed"  |  "Empty_Choice_Combinator"  |
//   "is_empty_choice"          |  "Select<>"

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>

namespace proto = ::crucible::safety::proto;

struct DummyResource {};
struct Payload {};

// Healthy base branch: send a Payload, then End.
using HealthyBase = proto::Send<Payload, proto::End>;

// Defective recovery branch: empty Select<> — no branch for the peer
// to `.pick<I>()` against on rollback.  Structurally well-formed (no
// stray Continue) but not runnable.
using EmptyRecovery = proto::Select<>;

using CheckpointedDefect = proto::CheckpointedSession<HealthyBase, EmptyRecovery>;

void compile_time_reject() {
    // mint_session_handle's is_empty_choice_v gate must fire on the
    // composite even though HealthyBase passes both checks on its
    // own — A2-004's recursive is_empty_choice specialization makes
    // the disjunction visible to the mint-level static_assert.
    auto h = proto::mint_session_handle<CheckpointedDefect>(DummyResource{});
    (void)h;
}

int main() { return 0; }
