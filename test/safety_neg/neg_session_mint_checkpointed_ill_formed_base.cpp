// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-024 mint-composition witness #1.  Demonstrates that
// `mint_session_handle<CheckpointedSession<B, R>>(resource)` fires
// the `is_well_formed_v` static_assert when B contains a free
// `Continue` (i.e., Continue outside any enclosing Loop).
//
// The composition story: `is_well_formed<CheckpointedSession<B, R>,
// LoopCtx>` (SessionCheckpoint.h:282-329) recurses into BOTH
// branches under the same LoopCtx; with LoopCtx=void (top-level),
// any Continue inside B fails `is_well_formed<Continue, void>`,
// which the disjunctive AND propagates upward to the
// CheckpointedSession trait, which the
// `static_assert(is_well_formed_v<Proto>, [Protocol_Ill_Formed])` at
// Session.h:2426-2430 rejects at the mint site.
//
// This fixture complements neg_session_mint_checkpointed_empty_choice_recovery.cpp
// (fixy-A2-024 mint-composition witness #2), which exercises the
// orthogonal `is_empty_choice_v` rejection.  Together they pin the
// two-gate composition documented in SessionCheckpoint.h's fixy-A2-024
// doc-block: well-formedness and empty-choice are orthogonal traits
// and BOTH fire at `mint_session_handle` for CheckpointedSession.
//
// Expected diagnostic family (one or more should match):
//   "static assertion failed"  |  "Protocol_Ill_Formed"  |
//   "is_well_formed"           |  "Continue"

#include <crucible/sessions/Session.h>
#include <crucible/sessions/SessionCheckpoint.h>

namespace proto = ::crucible::safety::proto;

struct DummyResource {};
struct Marker {};

// `CheckpointedSession<Continue, End>` — `Continue` appears in the
// base branch with no enclosing Loop.  Structurally ill-formed.
using IllFormedBase = proto::CheckpointedSession<proto::Continue, proto::End>;

void compile_time_reject() {
    // mint_session_handle fires is_well_formed_v first, before
    // is_empty_choice_v.  We expect [Protocol_Ill_Formed] —
    // CheckpointedSession's is_well_formed specialization recursed
    // into Continue's primary template, which is false_type without
    // a LoopCtx witness.
    auto h = proto::mint_session_handle<IllFormedBase>(DummyResource{});
    (void)h;
}

int main() { return 0; }
