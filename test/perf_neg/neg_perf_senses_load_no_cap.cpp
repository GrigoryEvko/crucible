// Negative-compile test. This file must fail to compile.
//
// Senses::load_all takes the startup load context, and the parameter has
// no default.  A call with no argument finds no matching function, so no
// caller loads the programs without the evidence of process startup.

#include <crucible/perf/Senses.h>

int main() {
    auto s = crucible::perf::Senses::load_all();
    (void)s;
    return 0;
}
