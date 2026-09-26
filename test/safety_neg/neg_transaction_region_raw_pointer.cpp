// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TransactionLog::commit takes the region as Transaction::ArenaRegion,
// which is fixy::Tagged<RegionNode*, source::Arena>.  A raw RegionNode*
// does not convert to it, because the constructor from the value is
// private and mint_tagged is the one door.
//
// Companion: neg_transaction_region_loaded_cross_tag.cpp refuses a region
// under a different source tag.

#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

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
    crucible::TransactionLog<16, Owner> log{::fixy::TestRunnerCtx{::foundation::effects::testing::test()}};
    auto* tx = log.begin_tx(owner, 1);
    crucible::RegionNode* region = nullptr;
    (void)log.commit(owner, tx, region, crucible::ContentHash{}, crucible::MerkleHash{1});
    return 0;
}
