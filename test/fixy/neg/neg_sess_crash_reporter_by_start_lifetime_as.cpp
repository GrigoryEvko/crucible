// A crash reporter started over a buffer.  No constructor of the reporter
// is trivial, so it is not an implicit-lifetime type, and the mandate of
// std::start_lifetime_as refuses it.

#include <fixy/session/CrashTransport.h>

#include <memory>

namespace s = ::fixy::session;

int main() {
    alignas(s::CrashReporter) unsigned char storage[sizeof(s::CrashReporter)]{};
    auto* forged = std::start_lifetime_as<s::CrashReporter>(storage);
    static_cast<void>(std::move(*forged).report(s::CrashCause::Abort));
    return 0;
}
