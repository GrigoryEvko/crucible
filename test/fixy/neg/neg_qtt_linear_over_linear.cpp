// Linear<Linear<T>> stacks the exactly-once obligation on a value that
// already carries it.  The second wrapper adds no guarantee and no new
// bug class, and it costs a consume at every layer.
//
// The rejection rests on the same structural recogniser as its Affine
// sibling: every grade of Qtt is recognised by one reflection query, so
// a wrapper nested in its own family cannot slip through the way it did
// while the table behind this gate stood empty.
//
// The second required diagnostic is the compiler's rendering of the
// instantiation, which the source never spells: it writes
// Linear<Linear<int>>.

#include <fixy/Qtt.h>

using LinearOverLinear = fixy::Linear<fixy::Linear<int>>;

static_assert(sizeof(LinearOverLinear) > 0);

int main() { return 0; }
