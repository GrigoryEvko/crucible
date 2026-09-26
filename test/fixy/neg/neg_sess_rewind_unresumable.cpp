// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A rewind opens a session at a position, and its gate asks the same of
// that session as a mint asks of a session from its start.  A Continue
// with no loop context names no position of any protocol, so the rewind
// refuses it.
//
// Expected diagnostic: no rewind accepts the position.
#include <fixy/session/Handle.h>

#include <utility>

namespace neg_sess_rewind_unresumable_types {
struct Wire {
    int words = 0;
};
}  // namespace neg_sess_rewind_unresumable_types

int main() {
    namespace s = ::fixy::session;
    using namespace neg_sess_rewind_unresumable_types;
    auto at_end = s::mint_session_handle<s::End, Wire>(Wire{});
    auto rewound = s::HandleFactory::rewind<s::Continue, void>(std::move(at_end));
    static_cast<void>(rewound);
    return 0;
}
