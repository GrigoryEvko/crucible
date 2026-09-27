// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// compose_at_branch is an alias template over the composition algebra of
// the registry, so no user specialization changes the branch that a
// composition reaches.
//
// Expected diagnostic: a specialization of an alias template.
#include <fixy/session/Protocol.h>

#include <cstddef>

namespace fixy::session {
template <typename Q>
struct compose_at_branch<Select<Send<int, End>, Send<long, End>>, std::size_t{0}, Q> {
    using type = Select<Send<int, End>, Send<long, End>>;
};
}  // namespace fixy::session

int main() { return 0; }
