// Every constructor that takes a bare value is private.  A Refined built
// directly from a value does not compile: the two mints are the only
// doors, so a search for them finds every admission.

#include <fixy/Refined.h>

int main() {
    fixy::Refined<fixy::positive, int> direct{42};
    return direct.value();
}
