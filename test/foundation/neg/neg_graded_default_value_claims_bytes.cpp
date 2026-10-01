// A default value in the stored regime would be paired with a grade that
// nothing witnesses.  Under a lattice whose grade is a claim about the
// bytes, the default constructor does not exist.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using GOneByte = fa::detail::GOneByte;

GOneByte const defaulted{};

}  // namespace

int main() { return defaulted.grade() ? 0 : 1; }
