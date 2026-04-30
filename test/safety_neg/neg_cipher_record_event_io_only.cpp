// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I09-AUDIT (Finding A) fixture — pins the Block leg of the
// row constraint on Cipher::record_event<CallerRow>.  Required row
// is Row<IO, Block>; a caller that holds Row<IO> alone is missing
// Block and must be rejected at substitution time.
//
// Why this matters: a refactor that "improves" the constraint to
// Subrow<Row<IO>, CallerRow> (only IO required) would silently
// accept this caller AND would remove the Block-fence.  The
// neg_pure_row fixture would still pass (Row<> still fails) but
// this fixture catches the silent widening.  The four per-axis
// fixtures (io_only / block_only / alloc_only / bg_only) ensure
// each leg of the {IO, Block} constraint is independently fenced.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<IO, Block>, Row<IO>>.

#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    // Caller declares Row<IO> — has IO, missing Block.
    //   {IO, Block} ⊄ {IO} → Subrow false → constraint fails.
    auto cipher = ::crucible::Cipher::open(
        "/tmp/crucible_neg_record_event_io_only");
    cipher.record_event<eff::Row<eff::Effect::IO>>(
        cipher.mint_open_view(),
        ::crucible::ContentHash{1u},
        std::uint64_t{1u});
    return 0;
}
