// A stored grade is a claim about the value, and a claim needs a key.
// The two-argument constructor that took the grade from anyone is gone,
// so braces that pair a value with a grade and name no authority have no
// constructor to reach.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using GOneByte = fa::detail::GOneByte;
using Value = fa::detail::OneByteValue;

GOneByte const forged{Value{}, true};

}  // namespace

int main() { return forged.grade() ? 0 : 1; }
