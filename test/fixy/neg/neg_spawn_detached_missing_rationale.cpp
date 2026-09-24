// join::Detached names a mechanism that never joins, so the pack has to
// carry a detach_with atom stating why.  An unjustified detach is what
// JoinPolicyGrantsCoherent exists to refuse.

#include <fixy/os/Spawn.h>

namespace join = fixy::spawn::join;

static_assert(fixy::spawn::JoinPolicyGrantsCoherent<join::Detached>,
              "a Detached mechanism with no detach_with atom must be refused");

int main() { return 0; }
