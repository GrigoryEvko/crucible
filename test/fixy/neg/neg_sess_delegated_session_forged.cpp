// A DelegatedSession holds a live endpoint.  Its value constructor is
// private, so code that holds a transferred endpoint still cannot build
// one: only mint_delegated_session does.

#include <fixy/session/Delegate.h>

#include <utility>

namespace neg_sess_delegated_session_forged_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
using Proto = s::Send<int, s::End>;
using Carried = s::DelegatedSession<Proto, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;

Carried forge(s::detail::transferred_endpoint<Wire>&& endpoint);

Carried forge(s::detail::transferred_endpoint<Wire>&& endpoint) {
    return Carried{std::move(endpoint)};
}

}  // namespace neg_sess_delegated_session_forged_types

int main() {
    return 0;
}
