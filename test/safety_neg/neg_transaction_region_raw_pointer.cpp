// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Transaction-2 (#1061): TransactionLog::commit admits the
// RegionNode dependency as Transaction::ArenaRegion =
// Tagged<RegionNode*, source::Arena>.  A raw RegionNode* must not
// cross the commit boundary.
//
// Expected diagnostic: no commit overload accepting raw RegionNode*.

#include <crucible/Transaction.h>

// A valid owner, so the commit is refused for its region argument alone.
class Owner {
public:
    static Owner claim() noexcept { return Owner{}; }
    Owner(const Owner&) = delete;
    ~Owner() {}

private:
    Owner() noexcept {}
};

int main() {
    const Owner owner = Owner::claim();
    crucible::TransactionLog<16, Owner> log{};
    auto* tx = log.begin_tx(owner, 1);
    crucible::RegionNode* region = nullptr;
    (void)log.commit(owner, tx, region, crucible::ContentHash{}, crucible::MerkleHash{1});
    return 0;
}
