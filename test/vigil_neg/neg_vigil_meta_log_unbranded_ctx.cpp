// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The metadata log is single-producer, and meta_log() hands out its
// producer surface.  So meta_log() asks for the context of this Vigil's
// producer claim, and a context that names no claim is refused.

#include <crucible/Vigil.h>

int main() {
    crucible::Vigil vigil;
    crucible::MetaLog& meta_log = vigil.meta_log(::foundation::effects::testing::foreground());
    (void)meta_log;
    return 0;
}
