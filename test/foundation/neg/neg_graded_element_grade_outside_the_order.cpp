// Where the value is its own grade, construction from the value alone
// takes no key, because the value cannot name a grade other than itself.
// It can still name no grade at all: 9 is a valid unsigned char and no
// element of a chain whose top is 3.  The constructor checks the value
// against the order as the keyed constructor checks a stored grade.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using ChainElement = fa::detail::GChainElement;

constexpr ChainElement outside_the_order{static_cast<unsigned char>(9)};

}  // namespace

int main() { return outside_the_order.grade(); }
