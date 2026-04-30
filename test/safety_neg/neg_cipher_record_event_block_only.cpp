// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-I09-AUDIT (Finding A) fixture — pins the IO leg of the
// row constraint on Cipher::record_event<CallerRow>.  Required row
// is Row<IO, Block>; a caller that holds Row<Block> alone is
// missing IO and must be rejected at substitution time.
//
// Symmetric to neg_cipher_record_event_io_only.cpp — together they
// ensure both atoms of {IO, Block} are independently load-bearing.
// A refactor that "improves" the constraint to Subrow<Row<Block>,
// CallerRow> (only Block required) would silently accept this
// caller AND would remove the IO-fence.  This fixture catches that.
//
// [GCC-WRAPPER-TEXT] — requires-clause constraint failure on
// Subrow<Row<IO, Block>, Row<Block>>.

#include <crucible/Cipher.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>

namespace eff = ::crucible::effects;

int main() {
    // Caller declares Row<Block> — has Block, missing IO.
    //   {IO, Block} ⊄ {Block} → Subrow false → constraint fails.
    auto cipher = ::crucible::Cipher::open(
        "/tmp/crucible_neg_record_event_block_only");
    cipher.record_event<eff::Row<eff::Effect::Block>>(
        cipher.mint_open_view(),
        ::crucible::ContentHash{1u},
        std::uint64_t{1u});
    return 0;
}
