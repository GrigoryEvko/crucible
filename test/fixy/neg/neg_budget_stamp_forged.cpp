// A budget stamp comes only from a budget authority.  Its constructor from
// two counts is private, so a producer cannot grant itself an allowance.

#include <fixy/Budgeted.h>

int main() {
    fixy::BudgetStamp const stamp{fixy::BitsBudgetLattice::bottom(), fixy::PeakBytesLattice::bottom()};
    return static_cast<int>(stamp.bits().raw());
}
