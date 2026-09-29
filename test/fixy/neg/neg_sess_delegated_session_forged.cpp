// A DelegatedSession holds a live endpoint and the hold of its tokens.
// Its constructor takes a DelegationKey, which only the delegation door
// makes, so code that holds a live handle and a hold still cannot build
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
using Handle = s::SessionHandle<Proto, Wire, void, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;
Carried forge(Handle&& handle, s::PermHold<fp::EmptyPermSet>&& hold);
Carried forge(Handle&& handle, s::PermHold<fp::EmptyPermSet>&& hold) {
    return Carried{std::move(handle), std::move(hold)};
}
}  // namespace neg_sess_delegated_session_forged_types

int main() { return 0; }
