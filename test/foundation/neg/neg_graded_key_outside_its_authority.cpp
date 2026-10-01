// A grade key is built inside a member of its authority and nowhere
// else.  A function that is no member of the class it names in the key
// meets the private constructor.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using GOneByte = fa::detail::GOneByte;
using Value = fa::detail::OneByteValue;

struct Stranger {};

GOneByte forge() { return GOneByte{fa::grade_key<Stranger>{}, Value{}, true}; }

}  // namespace

int main() { return forge().grade() ? 0 : 1; }
