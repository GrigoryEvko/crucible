// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The body of a callback entry holds a handle at End that carries the
// brand of the body, and it rewinds that handle.  The entry takes its End
// handle back, so a rewind of it would leave the entry with nothing to
// close.  The gate of the rewind refuses a handle that carries a brand.
//
// Expected diagnostic: no rewind accepts the branded handle.
#include <fixy/session/Handle.h>

#include <utility>

namespace neg_sess_rewind_branded_types {
struct Wire {
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
struct Body {
    template <typename Head>
    Head operator()(Head head) const {
        static_cast<void>(::fixy::session::HandleFactory::rewind<::fixy::session::End, void>(std::move(head)));
        return head;
    }
};
}  // namespace neg_sess_rewind_branded_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_rewind_branded_types;
    static_cast<void>(s::with_session<s::End, Wire>(Wire{}, Body{}));
    return 0;
}
