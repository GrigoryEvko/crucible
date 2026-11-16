// A binding at the linear Usage grade is consumed exactly once, so it has no
// copy.  The strict pole of the Usage axis is the linear grade, so a binding
// that names no Usage atom is linear.

#include <fixy/Fn.h>

int main() {
    auto const first = ::fixy::mint_fn<int>(1);
    auto const second = first;
    return second.value() - first.value();
}
