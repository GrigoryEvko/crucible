// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// dual_of is an alias template over the dual algebra of the registry, so
// no user specialization makes a Send the dual of a Send.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

namespace fixy::session {
template <>
struct dual_of<Send<int, End>> {
    using type = Send<int, End>;
};
}  // namespace fixy::session

int main() { return 0; }
