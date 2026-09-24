// The old wrapper had free(), which gave any payload the zero grade on
// both axes.  Zero is the strongest claim, so every gate admitted the
// value with no evidence of what it used.  The factory does not exist.
// A producer that measured its use states the measurement.

#include <fixy/Budgeted.h>

int main() {
    auto const claimed = fixy::Budgeted<int>::free(3);
    return claimed.peek();
}
