// A reference payload is refused.  The referent can be replaced after the
// claim is made, and a payload that used more would carry the smaller
// claim.

#include <fixy/Budgeted.h>

int main() {
    int target = 1;
    fixy::Budgeted<int&> const aliased{target, fixy::BitsBudget{8}, fixy::PeakBytes{8}};
    return aliased.peek();
}
