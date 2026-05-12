#include <crucible/canopy/Plumtree.h>

// Provokes PlumtreeShape: a broadcast substrate with no peer capacity cannot
// hold eager/lazy tree edges.
int main() {
    crucible::canopy::PlumtreeBroadcast<0, 8> broadcast{};
    return static_cast<int>(broadcast.link_count().value());
}
