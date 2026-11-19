// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TransactionLog::commit takes the region as Transaction::ArenaRegion,
// which is fixy::Tagged<Ref<RegionNode>, source::Arena>.  A raw RegionNode*
// does not convert to it, because the constructor from the value is
// private and mint_tagged is the one door.
//
// Companion: neg_transaction_region_loaded_cross_tag.cpp refuses a region
// under a different source tag.

#include <crucible/Transaction.h>
#include <fixy/Ctx.h>
#include <foundation/Quarantine.h>
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
    crucible::TransactionLog<16, Owner> log{};
    // The log stamps with its own count, so no call takes a tag.
    auto& tx = log.begin_tx(owner, 1);
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives a raw pointer to a typed door, and the door refuses it")
    crucible::RegionNode* region = nullptr;
    (void)log.commit(owner, tx, region, crucible::ContentHash{}, crucible::MerkleHash{1});
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return 0;
}
