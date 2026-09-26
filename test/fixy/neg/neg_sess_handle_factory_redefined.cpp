// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HandleKey befriends the handle factory, and fixy/session/Handle.h
// defines the factory beside the key.  A translation unit that includes
// the header and defines a class of that name to take the friendship gets
// a redefinition error.
//
// Expected diagnostic: a redefinition of the handle factory.
#include <fixy/session/Handle.h>

namespace fixy::session {
class HandleFactory final {};
}  // namespace fixy::session

int main() { return 0; }
