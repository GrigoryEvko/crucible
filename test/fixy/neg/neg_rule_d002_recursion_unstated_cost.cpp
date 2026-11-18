// D002: unbounded recursion x an unstated cost.  The binding states no
// Complexity atom, so it takes the strict pole, which states no cost.  A
// recursion with no depth bound and no cost bound has a frame count that
// nothing limits, so the rule refuses it as it refuses an explicit
// unbounded cost.

#include <fixy/Fn.h>

using Refused = ::fixy::fn<int, ::fixy::atom::dispatch::recurses_unbounded>;

static_assert(sizeof(Refused) > 0);

int main() { return 0; }
