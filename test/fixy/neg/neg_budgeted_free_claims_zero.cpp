// Budgeted has no free().  Such a factory would give any payload the
// zero grade on both axes.  Zero is the strongest claim, so every gate
// would admit the value with no evidence of what it used.  A producer
// that measured its use states the measurement.

#include <fixy/Budgeted.h>

int main() {
    auto const claimed = fixy::Budgeted<int>::free(3);
    return claimed.peek();
}
