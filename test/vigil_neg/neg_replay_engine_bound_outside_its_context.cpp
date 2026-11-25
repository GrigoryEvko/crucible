// Negative-compile test.  This file must fail to compile.
//
// The test door of a context gives its engine.  A test that binds that engine to
// a pool of its own could end the pool while the engine still reads its column.
// So only the context binds its engine to a pool, and the pool is its own.

#include <crucible/CrucibleContext.h>

int main() {
    auto const test = ::foundation::effects::testing::test();
    ::fixy::BumpArena<crucible::DagArena> arena = ::fixy::mint_bump_arena<crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    const crucible::DagRef<crucible::RegionNode> region =
        crucible::mint_dag_node<crucible::RegionNode>(test.alloc, arena);
    crucible::CrucibleContext ctx{test.alloc};
    crucible::PoolAllocator pool;
    ctx.engine(test).init_(*region, pool);
    return 0;
}
