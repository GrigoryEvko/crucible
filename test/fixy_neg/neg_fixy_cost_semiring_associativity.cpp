// ── neg_fixy_cost_semiring_associativity (FIXY-G11 HS14) ─────────────
//
// Pins the cost-semiring associativity law: a malformed claim about
// the semiring's algebraic shape fails to compile.
//
// We compute three associations of seq_compose: (A+B)+C and A+(B+C).
// The standard semiring lawful association `(A+B)+C == A+(B+C)` is
// pinned by static_assert in CostSemiring.h's self-test.  This fixture
// inverts the claim — asserts (A+B)+C and (A+B) are the SAME type,
// which is false because C contributes coefficients.  Build red is
// the expected outcome.

#include <crucible/algebra/CostSemiring.h>

#include <type_traits>

namespace ca = crucible::algebra;

namespace {

using A = ca::CostPolynomial<5, 3>;
using B = ca::CostPolynomial<7, 11>;
using C = ca::CostPolynomial<2, 4>;

// Bad-shape claim: assert (A+B)+C == (A+B).  This claims C is the
// additive identity, which it isn't — C has non-zero coefficients.
using AB = ca::seq_compose_t<A, B>;
using AB_C = ca::seq_compose_t<AB, C>;

// Build red — claim AB_C and AB are the same type.  They aren't:
// AB_C is CostPolynomial<14, 18>; AB is CostPolynomial<12, 14>.
static_assert(std::is_same_v<AB_C, AB>,
    "Cost-semiring fixture: a malformed claim that C is the additive "
    "identity must fail — seq_compose<seq_compose<A, B>, C> is NOT "
    "the same type as seq_compose<A, B> when C carries non-zero "
    "coefficients.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
