// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Transaction-2 (#1061): TransactionLog::commit admits the
// RegionNode dependency as Transaction::ArenaRegion =
// Tagged<RegionNode*, source::Arena>.  A raw RegionNode* must not
// cross the commit boundary.
//
// Expected diagnostic: no commit overload accepting raw RegionNode*.

#include <crucible/Transaction.h>

int main() {
  crucible::TransactionLog<16> log{};
  auto* tx = log.begin_tx(1);
  crucible::RegionNode* region = nullptr;
  (void)log.commit(tx, region, crucible::ContentHash{}, crucible::MerkleHash{1});
  return 0;
}
