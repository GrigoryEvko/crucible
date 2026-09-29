// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A translation unit specializes a class template that sits beside the
// handle factory, DelegatedSession, and calls a builder of the factory
// from the specialization.  The factory befriends no class template, so
// the specialization gets no access, and the builder stays private.  A
// builder in reach would give a handle at any protocol with a permission
// set that no token backs.
//
// Expected diagnostic: the builder is private in this context.
#include <fixy/session/Delegate.h>

namespace neg_sess_handle_factory_friend_specialized_types {
struct ForgeTag {};
struct Wire {
    int words = 0;
};
struct Region {};
}  // namespace neg_sess_handle_factory_friend_specialized_types

namespace fixy::session {
template <>
class DelegatedSession<neg_sess_handle_factory_friend_specialized_types::ForgeTag, int, check::Enforced,
                       ::foundation::permissions::EmptyPermSet> {
public:
    static auto forge(neg_sess_handle_factory_friend_specialized_types::Wire wire) {
        return HandleFactory::make_<
            Recv<int, End>, neg_sess_handle_factory_friend_specialized_types::Wire, void, check::Enforced,
            ::foundation::permissions::PermSet<neg_sess_handle_factory_friend_specialized_types::Region>>(wire);
    }
};
}  // namespace fixy::session

int main() { return 0; }
