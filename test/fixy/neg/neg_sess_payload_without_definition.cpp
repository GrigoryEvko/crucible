// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The rule of a payload stands in the header that defines the payload
// (fixy/session/Registry.h).  A payload class that is only declared here
// can have a rule in the header that defines it, so a query that read "no
// rule" here would give the payload a different meaning than a unit that
// sees the definition.  The query of the rule stops the build.
//
// Expected diagnostic: a payload class has no definition here.

#include <fixy/session/Protocol.h>

namespace {
struct Declared;
}  // namespace

int main() { return ::fixy::session::is_well_formed_v<::fixy::session::Send<Declared, ::fixy::session::End>> ? 0 : 1; }
