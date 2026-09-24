// A handle inside a Loop does not state the rest of its protocol, because
// its Continue refers to the loop context.  So it cannot travel as a
// DelegatedSession.

#include <fixy/session/Delegate.h>

namespace neg_sess_delegation_handle_in_loop_types {

namespace s = ::fixy::session;

struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Forever = s::Loop<s::Send<int, s::Continue>>;

}  // namespace neg_sess_delegation_handle_in_loop_types

int main() {
    namespace t = neg_sess_delegation_handle_in_loop_types;
    auto handle = t::s::mint_session_handle<t::Forever, t::Wire>(t::Wire{});
    auto carried = t::s::mint_delegated_session(std::move(handle));
    static_cast<void>(carried);
    return 0;
}
