// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-021: a SWMR reader role cannot publish writer-side values.

#include <crucible/sessions/SwmrSession.h>

namespace ses = crucible::safety::proto::swmr_session;

struct WriterTag {};
struct ReaderTag {};
using Swmr = ses::SwmrSession<int, WriterTag, ReaderTag>;

int main() {
    Swmr swmr{};
    auto reader = ses::mint_swmr_reader<Swmr>(swmr);
    return reader->send(7);
}
