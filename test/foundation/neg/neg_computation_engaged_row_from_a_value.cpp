// An engaged row is built only by the witnessed mint, which reads the row
// of a context.  The constructor from a value is private, so no caller
// claims IO for a value without a context that owns IO.
//
// Expected diagnostic: the constructor from a value is private.

#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::Computation<fe::Row<fe::Effect::IO>, int> forged{5};
    static_cast<void>(forged);
    return 0;
}
