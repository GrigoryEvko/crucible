// Weakening moves up the lattice and never down.  The in-body
// CRUCIBLE_PRE in Graded::weaken rejects a downward grade during
// constant evaluation, under every contract evaluation semantic.  The
// pre() clause it replaces was skipped at consteval under observe,
// which is the Release default, and under ignore.

#include <foundation/algebra/Graded.h>

namespace {

namespace fa = ::foundation::algebra;
using Chain = fa::detail::graded_self_test::TrivialChainLattice;
using Value = fa::detail::graded_self_test::OneByteValue;
using GradedByte = fa::Graded<fa::ModalityKind::Absolute, Chain, Value>;

constexpr GradedByte high{Value{}, static_cast<unsigned char>(3)};

// 1 sits below 3 in the chain, so this weakening runs the wrong way.
constexpr GradedByte lowered = high.weaken(static_cast<unsigned char>(1));

}  // namespace

int main() { return lowered.grade(); }
