// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A Published region gives the region only as const (fixy/handle/Published.h),
// and a const node gives each node that it links only as const
// (crucible/MerkleDag.h).  So no walk from a published region reaches a node
// that it can write: the body of a loop is
// const.

#include <crucible/MerkleDag.h>
#include <fixy/handle/Published.h>

namespace {

struct PublishesRegions {
    using published_type = crucible::RegionNode;
};

}  // namespace

int main() {
    crucible::RegionNode region{};
    const auto published = ::fixy::handle::mint_published(PublishesRegions{}, region);
    for (const ::fixy::Ref<crucible::TraceNode const> continuation : published.get().next) {
        for (const ::fixy::Ref<crucible::LoopNode const> loop : continuation->as_loop()) {
            for (const ::fixy::Ref<crucible::TraceNode>& first : loop->body)
                first->merkle_hash = crucible::MerkleHash{};
        }
    }
    return 0;
}
