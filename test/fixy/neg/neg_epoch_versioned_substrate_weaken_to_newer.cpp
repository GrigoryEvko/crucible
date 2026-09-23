// The substrate under EpochVersioned weakens toward the older version
// only.  A weaken to a newer version would mark an old payload fresh, and
// the CRUCIBLE_PRE guard in Graded::weaken refuses it during constant
// evaluation, because the version order is the dual one.

#include <fixy/EpochVersioned.h>

namespace {

using Grade = fixy::EpochVersioned<int>::graded_type;
using Version = fixy::EpochVersioned<int>::version_t;

constexpr Grade stale{1, Version{fixy::Epoch{3}, fixy::Generation{1}}};
static_assert(stale.weaken(Version{fixy::Epoch{9}, fixy::Generation{9}}).peek() == 1);

}  // namespace

int main() { return 0; }
