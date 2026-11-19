// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// TransactionLog::commit requires a Ref of a region of the arena.  A Ref
// tagged as Loaded from serialized state is a distinct provenance lane and
// cannot substitute for source::Arena.  The fixture makes the Loaded Ref as
// the loader makes it, from Replayed along the discharge edge, so the two
// types differ in the tag alone.
//
// Companion: neg_transaction_region_raw_pointer.cpp refuses an untagged
// region.

#include <crucible/MerkleDag.h>
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
    crucible::TransactionLog<16, Owner> log{};
    // The log stamps with its own count, so no call takes a tag.
    auto& tx = log.begin_tx(owner, 1);
    crucible::RegionNode region{};
    const auto loaded =
        ::fixy::mint_tagged<::fixy::tags::source::Replayed>(::fixy::Ref<crucible::RegionNode const>{::fixy::to(region)})
            .retag<::fixy::tags::source::Loaded>();
    (void)log.commit(owner, tx, loaded, crucible::ContentHash{}, crucible::MerkleHash{1});
    return 0;
}
