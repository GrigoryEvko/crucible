// A payload with a mutable member is refused.  peek() returns a const
// reference, a mutable member is writable through one, and a payload that
// used more could then replace the measured one under its grade.

#include <fixy/Budgeted.h>

struct Scratchpad {
    int value = 0;
    mutable int scratch = 0;
};

int main() {
    fixy::Budgeted<Scratchpad> const measured{Scratchpad{}, fixy::BitsBudget{8}, fixy::PeakBytes{16}};
    return measured.peek().value;
}
