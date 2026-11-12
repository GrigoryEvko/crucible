// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An explicit specialization of Loop gives its body member a protocol
// that its template argument does not give.  The fold reads the argument,
// a reception, and a reader of the body member would step into a send.
// The mint refuses the node whose member disagrees with its argument.
//
// Expected diagnostic: Protocol_Specialized_Combinator.
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>
#include <foundation/Quarantine.h>

namespace neg_sess_combinator_specialized_member_types {
using Receives = ::fixy::session::Recv<int, ::fixy::session::Continue>;
struct Wire {};
}  // namespace neg_sess_combinator_specialized_member_types

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture forges a Loop whose body member disagrees with its argument")
template <>
struct fixy::session::Loop<neg_sess_combinator_specialized_member_types::Receives> {
    using body = ::fixy::session::Send<int, ::fixy::session::Continue>;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_combinator_specialized_member_types;
    auto handle = s::mint_session_handle<s::Loop<Receives>>(Wire{});
    std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    return 0;
}
