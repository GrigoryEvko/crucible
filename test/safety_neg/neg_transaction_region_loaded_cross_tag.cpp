// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Transaction-2 (#1061): TransactionLog::commit requires an
// arena-owned RegionNode pointer.  A RegionNode* tagged as Loaded
// from serialized state is a distinct provenance lane and cannot
// substitute for source::Arena.
//
// Expected diagnostic: no conversion from LoadedRegionNode to ArenaRegion.

#include <crucible/ir001/Serialize.h>
#include <crucible/ir001/Transaction.h>

int main() {
  crucible::TransactionLog<16> log{};
  auto* tx = log.begin_tx(1);
  crucible::LoadedRegionNode loaded{nullptr};
  (void)log.commit(tx, loaded, crucible::ContentHash{}, crucible::MerkleHash{1});
  return 0;
}
