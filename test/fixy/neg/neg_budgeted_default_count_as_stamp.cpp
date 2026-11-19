// A default count is zero, the bottom of a use axis, and so the strongest
// claim of that axis.  The proof of a budget is a stamp of a budget
// authority, and a count does not convert to a stamp.  So a default count
// at the place of the stamp does not grade a value.

#include <fixy/Budgeted.h>

int main() {
    ::fixy::Budgeted<int> const claimed{7, ::fixy::BitsBudget{}};
    return claimed.peek();
}
