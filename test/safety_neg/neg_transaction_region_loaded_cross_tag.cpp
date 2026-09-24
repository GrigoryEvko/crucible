// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-Transaction-2 (#1061): TransactionLog::commit requires an
// arena-owned RegionNode pointer.  A RegionNode* tagged as Loaded
// from serialized state is a distinct provenance lane and cannot
// substitute for source::Arena.
//
// Expected diagnostic: no conversion from LoadedRegionNode to ArenaRegion.

#include <crucible/Serialize.h>
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
    crucible::LoadedRegionNode loaded{nullptr};
    (void)log.commit(owner, tx, loaded, crucible::ContentHash{}, crucible::MerkleHash{1});
    return 0;
}
