// A default value makes no claim only at the empty row.  At an engaged
// row it would claim an effect that nothing exercised, so the default
// constructor admits the empty row alone.
//
// Expected diagnostic: the default constructor's constraint refuses the
// IO row.

#include <foundation/effects/Computation.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::Computation<fe::Row<fe::Effect::IO>, int> forged;
    static_cast<void>(forged);
    return 0;
}
