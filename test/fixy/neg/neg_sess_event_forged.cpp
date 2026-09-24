// An event exists only through a factory or through the decoder, both of
// which produce valid events.  A caller cannot build one with a control
// byte of its own choosing.

#include <fixy/session/Recording.h>

namespace s = fixy::session;

namespace {
struct Wire {};
}  // namespace

int main() {
    s::SessionEvent event{s::SessionOp::Stop, s::RoleTagId{1}, s::RoleTagId{2}, 0, 0, 0, 200};
    (void)event;
    return 0;
}
