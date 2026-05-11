// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-214 fixture #3: source::Local and source::Gossiped are distinct
// CRDT lanes.  Locally-authored state cannot masquerade as received
// gossip state at merge.

#include <crucible/canopy/Crdt.h>

int main() {
    crucible::canopy::GSet<int, 8> set;
    crucible::canopy::LocalWrite<typename decltype(set)::state_type> local{
        set.state()};
    (void)set.merge(local);
    return 0;
}
