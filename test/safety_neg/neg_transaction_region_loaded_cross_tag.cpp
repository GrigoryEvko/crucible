// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TransactionLog::commit requires an arena-owned RegionNode pointer.  A
// RegionNode* tagged as Loaded from serialized state is a distinct
// provenance lane and cannot substitute for source::Arena.
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
    crucible::Arena arena{1024};
    const std::optional<crucible::LoadedRegionNode> loaded =
        crucible::deserialize_region(::foundation::effects::testing::test().alloc, std::span<const std::uint8_t>{}, arena);
    (void)log.commit(owner, tx, *loaded, crucible::ContentHash{}, crucible::MerkleHash{1});
    return 0;
}
