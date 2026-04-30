// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I16 fixture — pins TraceRing::try_append_pure rejects a
// caller declaring Row<Effect::Init>.
//
// IsPure<R> = Subrow<R, Row<>>.  Init is the one-shot startup
// context — Vigil::init, Keeper bootstrap, Cipher tier construction.
// The hot per-op recording path must never be invoked from Init
// context (init is the moment BEFORE the ring exists logically).
// Compile-time rejection catches accidental cross-tier wiring.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// IsPure<Row<Effect::Init>>.

#include <crucible/TraceRing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    crucible::TraceRing ring;
    crucible::TraceRing::Entry e{};
    (void)ring.try_append_pure<eff::Row<eff::Effect::Init>>(e);
    return 0;
}
