// A DelegatedSession holds a live endpoint.  Its value constructor is
// private, so code that holds a transferred endpoint still cannot build
// one: only mint_delegated_session does.

#include <fixy/session/Delegate.h>

namespace neg_sess_delegated_session_forged_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Proto = s::Send<int, s::End>;
using Carried = s::DelegatedSession<Proto, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;

}  // namespace neg_sess_delegated_session_forged_types

int main() {
    namespace t = neg_sess_delegated_session_forged_types;
    auto handle = t::s::mint_session_handle<t::Proto, t::Wire>(t::Wire{});
    auto endpoint = t::s::detail::endpoint_transfer::take(std::move(handle));
    t::Carried forged{std::move(endpoint)};
    return forged.holds_endpoint() ? 0 : 1;
}
