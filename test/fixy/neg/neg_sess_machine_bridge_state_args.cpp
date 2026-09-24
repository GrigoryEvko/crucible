// The bridge's mint builds the State from its arguments, so arguments
// the State cannot be built from are refused there.
//
// The protocol is well-formed and runnable, so only the constructibility
// clause can reject this call.

#include <fixy/session/MachineBridge.h>

namespace s = fixy::session;

namespace {
struct Report {};

// No constructor takes an int, and the aggregate's one member is not an
// int either, so Payload{7} is not valid initialization.
struct Payload {
    explicit Payload(const char* name) noexcept : name_{name} {}
    const char* name_;
};
}  // namespace

using Once = s::Send<Report, s::End>;

int main() {
    auto bridge = s::mint_session_from_machine<Once, Payload>(7);
    (void)bridge;
    return 0;
}
