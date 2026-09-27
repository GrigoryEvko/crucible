// A crash reporter built from bytes would let a transport report the crash
// of an endpoint that is still alive.  The move constructor of the
// reporter is user-provided, so the class is not trivially copyable, and
// std::bit_cast has no candidate.  The one door is mint_crash_reporter.

#include <fixy/session/CrashTransport.h>

#include <array>
#include <bit>
#include <cstddef>

namespace s = ::fixy::session;

int main() {
    const std::array<std::byte, sizeof(s::CrashReporter)> bytes{};
    auto forged = std::bit_cast<s::CrashReporter>(bytes);
    static_cast<void>(std::move(forged).report(s::CrashCause::Abort));
    return 0;
}
