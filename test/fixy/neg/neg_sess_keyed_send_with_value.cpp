// A Send of a Labelled is keyed: its label word is the whole message, so
// the step sends no value.  A call that passes a value is refused, and the
// diagnostic names the form to call.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>

#include <cstddef>

namespace s = fixy::session;

namespace {
struct Hello {};
struct Wire {};
}  // namespace

using Greet = s::Send<s::Labelled<Hello, int>, s::End>;

int main() {
    auto handle = s::mint_session_handle<Greet, Wire>(Wire{});
    auto done = std::move(handle).send(s::Labelled<Hello, int>{}, [](Wire&, auto&&) noexcept {});
    (void)std::move(done).close();
    return 0;
}
