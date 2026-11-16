// A snapshot of a ring reads the index of the other side, and the schedule
// decides when that side moves it.  So two runs with the same inputs can read
// two values, and the snapshot takes the bare tag of Nondet.  A body whose
// context holds no Nondet has no tag, and a call with no tag does not
// compile.
//
// Expected diagnostic: empty_approx has no candidate with no argument.

#include <fixy/concurrent/SpscRing.h>

int main() {
    ::fixy::concurrent::SpscRing<int, 8> ring;
    return ring.empty_approx() ? 0 : 1;
}
