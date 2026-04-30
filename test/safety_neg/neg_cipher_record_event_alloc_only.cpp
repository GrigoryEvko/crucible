// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I09-AUDIT (Finding A) fixture — pins that record_event
// rejects callers with completely unrelated atoms.  Required row
// is Row<IO, Block>; a caller that declares Row<Alloc> has neither
// IO nor Block and must be rejected.
//
// Why this matters: an arena-allocating caller (Row<Alloc>) is a
// realistic scenario that must NOT be allowed to write to HEAD/log
// files.  Allocators belong to the AllocClass axis; the IO/Block
// axes are orthogonal.  This fixture pins that orthogonality at
// the constraint level — a caller cannot "almost" satisfy
// record_event by holding the wrong axis.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<IO, Block>, Row<Alloc>>.

#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    // Caller declares Row<Alloc> — wrong axis entirely.
    //   {IO, Block} ⊄ {Alloc} → Subrow false → constraint fails.
    auto cipher = ::crucible::Cipher::open(
        "/tmp/crucible_neg_record_event_alloc_only");
    cipher.record_event<eff::Row<eff::Effect::Alloc>>(
        cipher.mint_open_view(),
        ::crucible::ContentHash{1u},
        std::uint64_t{1u});
    return 0;
}
