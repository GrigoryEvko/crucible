// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I09-AUDIT (Finding A) fixture — pins that record_event
// rejects a caller that declares only Effect::Bg.  This is the
// SUBTLEST rejection: Bg is the bg-thread context tag, but it
// does NOT type-imply IO or Block at the row level.  A bg thread
// that hasn't explicitly opted into IO/Block caps cannot record
// events.
//
// Why this matters: the Bg context struct (effects::Bg) IS the
// canonical way to mint IO + Block in production code, but at the
// type-row level it's three separate atoms (Bg, IO, Block).  A
// caller that declares ONLY Row<Bg> (perhaps a misguided refactor
// that shortened a Row<Alloc, IO, Block, Bg> to "just Row<Bg>" on
// the assumption that Bg implies the rest) must be rejected.  This
// fixture pins the orthogonality of the Bg context-tag from the
// IO/Block capability atoms.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<IO, Block>, Row<Bg>>.

#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    // Caller declares Row<Bg> alone — Bg context tag without
    // IO/Block caps.  {IO, Block} ⊄ {Bg} → Subrow false.
    auto cipher = ::crucible::Cipher::open(
        "/tmp/crucible_neg_record_event_bg_only");
    cipher.record_event<eff::Row<eff::Effect::Bg>>(
        cipher.mint_open_view(),
        ::crucible::ContentHash{1u},
        std::uint64_t{1u});
    return 0;
}
