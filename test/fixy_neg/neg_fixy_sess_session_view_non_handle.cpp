// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// mint_session_view<Tag>(handle) carries `requires HandleIsAt<Handle, Tag>`.
// An int is not a SessionHandle, so no handle_is_at specialization matches
// and HandleIsAt is false for every Tag.  The static_assert below says the
// call is well formed, and it must fail.
//
// Expected diagnostic: static assertion failed / HandleIsAt /
// mint_session_view.

#include <crucible/sessions/_SessionView.h>

namespace sp = crucible::safety::proto;

namespace neg_fixy_sess_session_view_non_handle {
static_assert(requires(int const& h) { sp::mint_session_view<sp::AtSend>(h); },
              "NEG-COMPILE: a type that is not a SessionHandle (int) must not be viewable at any position "
              "through mint_session_view; this static_assert must fail.");
}  // namespace neg_fixy_sess_session_view_non_handle

int main() { return 0; }
