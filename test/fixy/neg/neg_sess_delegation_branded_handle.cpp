// A callback entry or a forked channel gives its body a handle that
// carries the brand of the body, and the body must return that handle.
// So the body cannot give it away as a DelegatedSession.

#include <fixy/session/Delegate.h>

#include <utility>

namespace neg_sess_delegation_branded_handle_types {

namespace s = ::fixy::session;

struct Wire {
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct Body {};
using Proto = s::Send<int, s::End>;
using Head = s::detail::first_handle_t<Proto, Wire, s::DefaultAbandonmentPolicy,
                                       ::foundation::permissions::EmptyPermSet, s::detail::brand_ctx_t<Body>>;
using Refused = decltype(s::mint_delegated_session(std::declval<Head>()));

}  // namespace neg_sess_delegation_branded_handle_types

int main() { return 0; }
