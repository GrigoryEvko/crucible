// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// PermHold and the loan markers befriend the hold factory, and
// fixy/session/Payload.h defines the factory beside them.  A translation
// unit that includes the header and defines a class of that name to take
// the friendship gets a redefinition error.
//
// Expected diagnostic: a redefinition of the hold factory.
#include <fixy/session/Payload.h>

namespace fixy::session {
class HoldFactory final {};
}  // namespace fixy::session

int main() { return 0; }
