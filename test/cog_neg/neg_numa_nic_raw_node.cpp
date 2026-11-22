// Negative compile test. This file must fail to compile.
//
// A NUMA node is a NumaNodeId, and its constructor from an integer is
// explicit. A raw topology number does not convert, so the unknown
// sentinel stays in one place.

#include <crucible/cog/NumaNic.h>

namespace cog = crucible::cog;

cog::NumaNicFacts facts{
    .nic_node = 0u,
};

int main() { return static_cast<int>(facts.nic_node.raw()); }
