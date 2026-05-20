// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-074m fixture #1 for fixy::sess::mint_session_view
// (SessionView.h:277).  mint_session_view<Tag>(handle) carries
// `requires HandleIsAt<Handle, Tag>`.  A SessionHandle parked at the
// Send position is NOT at AtRecv, so HandleIsAt rejects it and the call
// is ill-formed.
//
// We probe via `static_assert(requires { ... })` — the SAME discipline
// SessionView.h uses for its `can_mint_session_view` self-test (line
// 429: `static_assert(!can_mint_session_view<SendHandle, AtRecv>)`).
// This deliberately avoids constructing a namespace-scope Send-state
// SessionHandle instance, whose destructor would fire the abandonment
// check at program exit (documented at SessionView.h:255-261).  The
// assertion asserts the call IS well-formed and is EXPECTED TO FAIL.
//
// Distinct mismatch class from
// neg_fixy_sess_session_view_non_handle.cpp (#2): here the argument IS
// a SessionHandle but at the WRONG protocol position; there it is not a
// SessionHandle at all.
//
// Expected diagnostic: static assertion failed / HandleIsAt /
// mint_session_view.

#include <crucible/sessions/SessionView.h>
#include <crucible/fixy/Sess.h>

namespace sp    = crucible::safety::proto;
namespace fsess = crucible::fixy::sess;

namespace neg_fixy_sess_session_view_wrong_position {
struct Msg     {};
struct FakeRes {};

using SendHandle = fsess::SessionHandle<fsess::Send<Msg, fsess::End>, FakeRes>;

// mint_session_view<AtRecv> on a Send-position handle must be rejected by
// HandleIsAt — so this requires-expression is false and the assertion FAILS.
static_assert(
    requires(SendHandle const& h) { fsess::mint_session_view<sp::AtRecv>(h); },
    "NEG-COMPILE FIXY-U-074m: a Send-position SessionHandle must NOT be "
    "viewable at AtRecv via mint_session_view (HandleIsAt rejects); this "
    "static_assert is expected to FAIL.");
}  // namespace neg_fixy_sess_session_view_wrong_position

int main() { return 0; }
