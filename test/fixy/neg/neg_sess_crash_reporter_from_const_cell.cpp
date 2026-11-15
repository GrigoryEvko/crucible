// The survivor holds its detector cell as const: a view of the detector.
// A view gives no right to report a crash, so the reporter mint refuses a
// const cell.

#include <fixy/session/CrashTransport.h>

namespace s = ::fixy::session;

namespace {
struct Alice {};
struct Bob {};
}  // namespace

int main() {
    s::CrashCell<Alice, Bob> cell;
    const s::CrashCell<Alice, Bob>& view = cell;
    auto reporter = s::mint_crash_reporter(view);
    static_cast<void>(reporter);
    return 0;
}
