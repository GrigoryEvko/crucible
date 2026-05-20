// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074m fixture #2 for fixy::sess::mint_session_view
// (SessionView.h:277).  mint_session_view<Tag>(handle) carries
// `requires HandleIsAt<Handle, Tag>`.  A non-SessionHandle type (int)
// matches no handle_is_at specialization, so HandleIsAt is false for
// every Tag (SessionView.h self-test line: `!handle_is_at_v<int,
// AtSend>`) and the call is ill-formed.
//
// We probe via `static_assert(requires { ... })`, mirroring
// SessionView.h's can_mint_session_view self-test discipline; the
// assertion asserts the call IS well-formed and is EXPECTED TO FAIL.
//
// Distinct mismatch class from
// neg_fixy_sess_session_view_wrong_position.cpp (#1): here the argument
// is not a SessionHandle at all; there it IS a SessionHandle but parked
// at the wrong protocol position.
//
// Expected diagnostic: static assertion failed / HandleIsAt /
// mint_session_view.

#include <crucible/sessions/SessionView.h>
#include <crucible/fixy/Sess.h>

namespace sp    = crucible::safety::proto;
namespace fsess = crucible::fixy::sess;

namespace neg_fixy_sess_session_view_non_handle {
// int is not a SessionHandle — HandleIsAt<int, AtSend> is false, so this
// requires-expression is false and the assertion FAILS.
static_assert(
    requires(int const& h) { fsess::mint_session_view<sp::AtSend>(h); },
    "NEG-COMPILE FIXY-U-074m: a non-SessionHandle type (int) must NOT be "
    "viewable at any position via mint_session_view (HandleIsAt rejects); "
    "this static_assert is expected to FAIL.");
}  // namespace neg_fixy_sess_session_view_non_handle

int main() { return 0; }
