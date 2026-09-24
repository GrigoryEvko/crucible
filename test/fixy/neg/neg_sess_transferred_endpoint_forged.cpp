// A transferred endpoint carries a live record of the watch.  A value
// built from a Resource and a record that no handle gave up would claim
// a session it does not hold, so only endpoint_transfer builds one.

#include <fixy/session/Handle.h>

namespace {
namespace s = ::fixy::session;
struct Wire {};
}  // namespace

int main() {
    auto forged = s::detail::transferred_endpoint<Wire>{Wire{}, s::watch::session_ref{}};
    static_cast<void>(forged);
    return 0;
}
