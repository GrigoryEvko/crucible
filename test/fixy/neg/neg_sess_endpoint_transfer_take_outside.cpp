// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// endpoint_transfer takes the Resource and the watch record out of a live
// handle, and the record stays live.  A caller that is not the door of
// mint_delegated_session could take the endpoint and drop it, and the
// abandonment policy of the handle would never act.  The operation is
// private.
//
// Expected diagnostic: the operation is private in this context.

#include <fixy/session/Handle.h>

#include <utility>

namespace neg_sess_endpoint_transfer_take_outside_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_endpoint_transfer_take_outside_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_endpoint_transfer_take_outside_types;
    auto handle = s::mint_session_handle<s::Send<int, s::End>, Wire>(Wire{});
    auto taken = s::detail::endpoint_transfer::take(std::move(handle));
    return taken.resource.words;
}
