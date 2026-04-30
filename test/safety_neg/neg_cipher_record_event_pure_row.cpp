// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I09 fixture — pins the requires-clause on
// Cipher::record_event<CallerRow>.  The template parameter must
// satisfy `Subrow<Row<IO, Block>, CallerRow>`.  A caller in a
// Hot/Pure context (empty row, Row<>) cannot satisfy the constraint
// because {IO, Block} ⊄ {} — the substitution must fail loudly with
// a constraint diagnostic, NOT silently proceed.
//
// Why this matters: the 8th-axiom fence on record_event prevents
// foreground hot-path code from invoking the file-I/O side effect.
// Without this fence, a refactor that accidentally calls
// cipher.record_event(...) from a Hot context would compile cleanly
// AND silently break replay determinism on the hot path (each
// foreground iteration would do a blocking file write).  The fence
// catches that drift at substitution time.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<IO, Block>, Row<>>.

#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    // Hot/Pure context — empty row.  {IO, Block} ⊄ {} → fence fires.
    auto cipher = ::crucible::Cipher::open("/tmp/crucible_neg_record_event");
    cipher.record_event<eff::Row<>>(
        cipher.mint_open_view(),
        ::crucible::ContentHash{1u},
        std::uint64_t{1u});
    return 0;
}
