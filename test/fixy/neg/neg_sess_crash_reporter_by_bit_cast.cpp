// A crash reporter built from bytes would let a transport publish a crash
// report with a count of its own choosing.  The move constructor of the
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
    static_cast<void>(std::move(forged).report(s::CrashCause::Abort, s::MessageCount{1000}));
    return 0;
}
