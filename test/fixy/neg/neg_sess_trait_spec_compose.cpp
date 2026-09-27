// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// compose is an alias template over the composition algebra of the
// registry, so no user specialization drops the suffix of a composition.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

namespace fixy::session {
template <typename Q>
struct compose<Send<int, End>, Q> {
    using type = Send<int, End>;
};
}  // namespace fixy::session

int main() { return 0; }
