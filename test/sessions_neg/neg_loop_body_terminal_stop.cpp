// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 floor for the Loop<B> well-formedness check at
// include/crucible/sessions/_Session.h.
//
// `Stop` (the ungraded alias of `Stop_g<CrashClass::Abort>`) is a
// terminal state per `is_terminal_state` (_SessionCrash.h).
// Therefore `Loop<Stop>` is ill-formed by the same rule that rejects
// `Loop<End>`: the loop can never reach a Continue, the iteration
// counter cannot advance, and the construct is semantically
// equivalent to its terminal body.
//
// This fixture witnesses the Stop case.  Together with
// neg_loop_body_terminal_end.cpp it satisfies HS14 (≥2 negative-
// compile fixtures per gate) for the new `!is_terminal_state<B>`
// conjunct in the is_well_formed<Loop<B>, LoopCtx> specialization.
//
// Expected diagnostic family (one or more should match):
//   "static assertion failed"  |  "Protocol_Ill_Formed"  |
//   "is_well_formed"

#include <crucible/sessions/_Session.h>
#include <crucible/sessions/_SessionCrash.h>

namespace proto = ::crucible::safety::proto;

struct Wire {};

using TerminalLoopProto = proto::Loop<proto::Stop>;

[[maybe_unused]] void probe() {
    auto bad = proto::mint_session_handle<TerminalLoopProto>(Wire{});
    (void)bad;
}
