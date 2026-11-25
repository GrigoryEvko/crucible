// The keyed write of a grade takes the check of the keyed constructor.  A grade
// outside the bounds of the lattice fails its CRUCIBLE_PRE, which in a constant
// expression is a compile-time rejection located inside Graded.h.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using Chain = fa::detail::TrivialChainLattice;
using Value = fa::detail::OneByteValue;
using GradedByte = fa::Graded<fa::ModalityKind::Absolute, Chain, Value>;
using Authority = fa::detail::self_test_authority;

// 9 is a valid unsigned char and no element of a chain whose top is 3.
constexpr unsigned char set_outside_the_order() {
    GradedByte held{Authority::key(), Value{}, static_cast<unsigned char>(1)};
    held.set_grade(Authority::key(), static_cast<unsigned char>(9));
    return held.grade();
}

constexpr unsigned char outside_the_order = set_outside_the_order();

}  // namespace

int main() { return outside_the_order; }
