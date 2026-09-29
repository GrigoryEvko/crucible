// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// meta_log() hands out the producer surface of the metadata log, so it asks
// for the foreground context of a Vigil's producer claim.  The context of
// another state's claim proves that its holder owns that state, not a Vigil.

#include <crucible/Vigil.h>

namespace {
struct Stranger {};
}  // namespace

int main() {
    crucible::Vigil vigil;
    crucible::MetaLog& meta_log = vigil.meta_log(::foundation::effects::testing::foreground<Stranger>());
    (void)meta_log;
    return 0;
}
