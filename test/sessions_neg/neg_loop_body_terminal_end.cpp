// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A2-020: HS14 floor for the Loop<B> well-formedness check at
// include/crucible/sessions/Session.h.
//
// `Loop<B>` is structurally ill-formed when B itself is a terminal
// state — End / Stop / Stop_g<C> / VendorPinned<V, terminal> — because
// the loop can never reach a Continue and the iteration counter cannot
// advance.  Such a "loop" is semantically equivalent to its terminal
// body but masquerades as a fix-point.
//
// This fixture witnesses the End case: `Loop<End>` must fail
// is_well_formed_v<Proto>, and mint_session_handle must reject it at
// the static_assert site with the [Protocol_Ill_Formed] diagnostic.
//
// Expected diagnostic family (one or more should match):
//   "static assertion failed"  |  "Protocol_Ill_Formed"  |
//   "is_well_formed"

#include <crucible/sessions/Session.h>

namespace proto = ::crucible::safety::proto;

struct Wire {};

using TerminalLoopProto = proto::Loop<proto::End>;

[[maybe_unused]] void probe() {
    auto bad = proto::mint_session_handle<TerminalLoopProto>(Wire{});
    (void)bad;
}
