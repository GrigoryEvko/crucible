// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The class below stands for a decorator or a door that a new header of
// the session layer declares.  It is in a censused namespace, and the
// census tables give it no row.  So the census counts an unproven
// carrier, and it refuses the build.  The census includes each public
// header of the two layers, so such a class is in its roster from the
// commit that adds its header.  The census header compiles without this
// class (test_row_hash_wrappers includes it), so the class is the one
// reason that this file fails.  This file includes only the census, and
// not the other cells of the test.
//
// Expected diagnostic: a carrier in a censused namespace is unproven.

#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::session {
class UndisposedDoor {};
}  // namespace fixy::session
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

#include "../row_hash_census.h"
