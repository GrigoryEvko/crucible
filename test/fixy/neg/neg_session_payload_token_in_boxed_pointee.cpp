// A token laundered through a std::unique_ptr to a specialization that
// holds it in a member no template argument names.  The walk once read
// such a pointee for its arguments only, so the payload moved nothing and
// the sender kept the region while the recipient owned the token.  The
// walk reads the pointee for its members, and refuses the token behind
// the pointer.

#include <fixy/session/Payload.h>
#include <foundation/permissions/Permission.h>

#include <memory>

namespace sess = ::fixy::session;

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
using TX = sess::Transferable<int, Region>;

template <class T>
struct Box {
    T tag{};
    TX token;
};
}  // namespace

int main() {
    using Delta = sess::payload_perm_delta<std::unique_ptr<Box<int>>>;
    return static_cast<int>(sizeof(typename Delta::sender_requires));
}
