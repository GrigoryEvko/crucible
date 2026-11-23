// Negative compile test.  This file must fail to compile.
//
// mint_dag_node builds each node in an arena of the DAG family, so the node
// has the DagArena brand that its class states (crucible/MerkleDag.h).  An
// arena of a different family gives no node: the brand of the node would not
// name the arena that holds it.

#include <crucible/MerkleDag.h>

#include <cstddef>

namespace {

struct[[= ::foundation::brand::arena_family_brand{}]] OtherFamily final {};

}  // namespace

int main() {
    ::fixy::BumpArena<OtherFamily> arena =
        ::fixy::mint_bump_arena<OtherFamily>(::fixy::Quantity<::fixy::unit::Byte, std::size_t>{4096});
    const auto node =
        crucible::mint_dag_node<crucible::RegionNode>(::foundation::effects::testing::test().alloc, arena);
    return static_cast<int>(node->kind);
}
