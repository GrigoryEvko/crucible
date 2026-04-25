// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: make_session_handle called with Resource = lvalue
// reference to a NON-Pinned channel.  The SessionResource concept
// (added by integration task #406, misc/24_04_2026_safety_integration.md
// §17) rejects this at compile time because the non-Pinned object can
// be moved or destroyed under the live handle, producing a
// use-after-free at the next Send/Recv.
//
// Expected diagnostic substring:
//   crucible::session::diagnostic [SessionResource_NotPinned]

#include <crucible/sessions/Session.h>

using namespace crucible::safety::proto;

// A NON-Pinned channel.  Address is not stable — a move of this
// object would invalidate any reference the handle holds.
struct NonPinnedChannel {
    int placeholder = 0;
};

int main() {
    NonPinnedChannel ch{};

    // Explicit lvalue-reference Resource — the framework MUST reject.
    // Without the SessionResource constraint, the handle below would
    // store a NonPinnedChannel*; if `ch` were subsequently moved or
    // destroyed, the handle would dangle.
    [[maybe_unused]] auto h =
        make_session_handle<End, NonPinnedChannel&>(ch);

    return 0;
}
