// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The members_of of GCC 16 lists no alias member whose target is a closure
// type, so the member check reads such a member on a copy of the node with
// a stand-in.  An explicit specialization of Send for a closure payload
// gives its message_type member a different closure.  The specialization
// has its own source location, so the copy does not speak for it, and the
// mint refuses the node whose member disagrees with its argument.
//
// Expected diagnostic: Protocol_Specialized_Combinator.
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>
#include <foundation/Quarantine.h>

namespace neg_sess_combinator_closure_member_lie_types {
using Sent = decltype([] {});
using Claimed = decltype([](int) {});
struct Wire {};
}  // namespace neg_sess_combinator_closure_member_lie_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges a Send whose message_type is a different closure")
template <>
struct fixy::session::Send<neg_sess_combinator_closure_member_lie_types::Sent, ::fixy::session::End> {
    using message_type = neg_sess_combinator_closure_member_lie_types::Claimed;
    using next = ::fixy::session::End;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_combinator_closure_member_lie_types;
    auto handle = s::mint_session_handle<s::Send<Sent, s::End>>(Wire{});
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
