// Negative compile test.  This file must fail to compile.
//
// A node of the DAG states the brand of the DAG arenas, so to(node) gives a
// Ref of that brand (crucible/MerkleDag.h).  That is sound only because each
// node lives in a DAG arena: the constructor of a node takes the key that
// only mint_dag_node builds.  A node on the stack would carry the brand of an
// arena that does not hold it, so it does not compile.

#include <crucible/MerkleDag.h>

int main() {
    crucible::RegionNode region{};
    return static_cast<int>(region.kind);
}
