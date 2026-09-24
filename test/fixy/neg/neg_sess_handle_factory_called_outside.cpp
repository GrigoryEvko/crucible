// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The handle factory builds a handle at a protocol position and with a
// loop context that the caller names.  A call from a scope that is not a
// friend would give a handle that no mint admitted, for example a handle
// at End with the brand of a callback body, which the callback entry then
// accepts in place of its own.  The members of the factory are private.
//
// Expected diagnostic: the factory member is private in this context.

#include <fixy/session/Handle.h>

namespace neg_sess_handle_factory_called_outside_types {
struct Wire {
    int words = 0;
};
struct Body {};
}  // namespace neg_sess_handle_factory_called_outside_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_handle_factory_called_outside_types;
    auto forged =
        s::HandleFactory::make_<s::End, Wire, s::detail::session_brand<Body>, s::DefaultAbandonmentPolicy>(Wire{});
    return std::move(forged).close().words;
}
