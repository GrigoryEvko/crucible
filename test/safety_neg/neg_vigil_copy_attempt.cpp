// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-031 fixture #2: Vigil owns the publication slot, ModeCell, foreground
// context, and background thread. Copying it would duplicate those process-local
// identities and split the bg->fg publication discipline.

#include <crucible/Vigil.h>

int main() {
    ::crucible::Vigil first;
    ::crucible::Vigil second{first};
    return second.is_compiled() ? 0 : 1;
}
