// CapMatchesCtx admits a capability when the row of the context holds the
// effect that cap_of reads off the capability.  This file tries to give
// every capability the IO effect, so a context that owns IO would accept
// each one: it writes an explicit specialization of cap_of.  The reading is
// a function at namespace scope that is not a template, so no
// specialization matches it.

#include <foundation/effects/Capability.h>

#include <meta>

template <>
consteval foundation::effects::Effect foundation::effects::cap_of(std::meta::info) {
    return foundation::effects::Effect::IO;
}

int main() { return 0; }
