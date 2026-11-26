// Negative compile test.  This file must fail to compile.
//
// A Published region gives the region only as const (fixy/handle/Published.h),
// and a const node gives each node that it links only as const
// (crucible/MerkleDag.h).  So no walk from a published region reaches a node
// that it can write: the target of an arm of a
// branch is const.

#include <crucible/MerkleDag.h>
#include <fixy/handle/Published.h>

namespace {

struct PublishesRegions {
    using published_type = crucible::RegionNode;
};

}  // namespace

int main() {
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    const crucible::DagRef<crucible::RegionNode> region =
        ::foundation::core::with_arena_frame(arena, [](auto& frame) noexcept {
            return crucible::mint_dag_node<crucible::RegionNode>(::foundation::effects::testing::test().alloc, frame)
                .weaken();
        });
    const auto published = ::fixy::handle::mint_published(PublishesRegions{}, region);
    for (const crucible::DagRef<crucible::TraceNode const> continuation : published.get().next) {
        for (const crucible::DagRef<crucible::BranchNode const> branch : continuation->as_branch()) {
            for (crucible::BranchNode::Arm const& arm : branch->arms()) {
                for (const crucible::DagRef<crucible::TraceNode>& target : arm.target())
                    target->merkle_hash = crucible::MerkleHash{};
            }
        }
    }
    return 0;
}
