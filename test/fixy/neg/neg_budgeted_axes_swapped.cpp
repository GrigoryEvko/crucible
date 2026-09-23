// The two grades are two types.  With raw integers, a peak of 4096 bytes
// passed where the bit count belongs would be stored as the bit count,
// and the gate would then compare it against the bit threshold.  The
// constructor takes BitsBudget and then PeakBytes, and nothing converts
// one into the other.

#include <fixy/Budgeted.h>

int main() {
    fixy::Budgeted<int> const swapped{1, fixy::PeakBytes{4096}, fixy::BitsBudget{8}};
    return swapped.peek();
}
