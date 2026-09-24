// Every constructor that takes a bare value is private.  Constructing a
// Refined directly, as the old spelling did, no longer compiles: the
// two mints are the only doors, so a search for them finds every
// admission.

#include <fixy/Refined.h>

int main() {
    fixy::Refined<fixy::positive, int> direct{42};
    return direct.value();
}
