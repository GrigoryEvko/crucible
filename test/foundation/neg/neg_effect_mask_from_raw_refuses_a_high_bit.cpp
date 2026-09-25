// Bit 6 names no effect atom.  A mask built from it during constant
// evaluation reaches the trap in from_raw, so the value never becomes a
// constant with a poison bit.

#include <foundation/effects/Row.h>

namespace fe = ::foundation::effects;

constexpr fe::EffectMask poisoned = fe::EffectMask::from_raw(fe::EffectMask::underlying_type{1} << 6);

int main() { return poisoned.none() ? 0 : 1; }
