// A Budgeted takes a finite grade only from a stamp of a budget authority.
// The constructor from a value and a grade is private, so no caller states
// the zero budget, the strongest claim, for a value that nothing measured.

#include <fixy/Budgeted.h>

int main() {
    using Value = ::fixy::Budgeted<int>;
    Value const claimed{7, Value::budget_t{::fixy::BitsBudgetLattice::bottom(), ::fixy::PeakBytesLattice::bottom()}};
    return claimed.peek();
}
