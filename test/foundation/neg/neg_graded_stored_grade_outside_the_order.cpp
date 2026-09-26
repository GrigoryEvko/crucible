// The primary Graded stores the grade a caller hands in, and nothing
// else witnesses it.  A grade outside the lattice's bounds fails the
// keyed constructor's CRUCIBLE_PRE, which in a constant expression is a
// compile-time rejection located inside Graded.h.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using Chain = fa::detail::graded_self_test::TrivialChainLattice;
using Value = fa::detail::graded_self_test::OneByteValue;
using GradedByte = fa::Graded<fa::ModalityKind::Absolute, Chain, Value>;
using Authority = fa::detail::graded_self_test::self_test_authority;

// 9 is a valid unsigned char and no element of a chain whose top is 3.
constexpr GradedByte outside_the_order{Authority::key(), Value{}, static_cast<unsigned char>(9)};

}  // namespace

int main() { return outside_the_order.grade(); }
