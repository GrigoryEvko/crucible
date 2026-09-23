// The substrate under Budgeted weakens toward more use only.  A weaken to
// less use would claim a tighter budget than was measured, and the
// CRUCIBLE_PRE guard in Graded::weaken refuses it during constant
// evaluation.

#include <fixy/Budgeted.h>

namespace {

using Grade = fixy::Budgeted<int>::graded_type;
using Budget = fixy::BudgetLattice::element_type;

constexpr Grade measured{1, Budget{fixy::BitsBudget{64}, fixy::PeakBytes{64}}};
static_assert(measured.weaken(Budget{fixy::BitsBudget{0}, fixy::PeakBytes{0}}).peek() == 1);

}  // namespace

int main() { return 0; }
