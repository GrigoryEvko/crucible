// A DelegatedSession started over a buffer.  Its destructor and its move
// constructor are not trivial, so it is not an implicit-lifetime type, and
// the mandate of std::start_lifetime_as refuses it.

#include <fixy/session/Delegate.h>

#include <memory>

namespace neg_sess_delegated_session_forged_by_start_lifetime_as_types {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;

struct Wire {};
using Carried = s::DelegatedSession<s::End, Wire, s::DefaultAbandonmentPolicy, fp::EmptyPermSet>;

}  // namespace neg_sess_delegated_session_forged_by_start_lifetime_as_types

int main() {
    namespace t = neg_sess_delegated_session_forged_by_start_lifetime_as_types;
    alignas(t::Carried) unsigned char storage[sizeof(t::Carried)]{};
    auto* forged = std::start_lifetime_as<t::Carried>(storage);
    return forged->holds_endpoint() ? 0 : 1;
}
