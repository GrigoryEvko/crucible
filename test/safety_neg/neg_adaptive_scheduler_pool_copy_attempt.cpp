// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Pool<Policy> owns worker jthreads, queue state, mutexes, and atomic
// counters.  Copying it would duplicate the control surface without
// duplicating the running workers' identity.

#include <crucible/concurrent/AdaptiveScheduler.h>

int main() {
    namespace cc = crucible::concurrent;
    namespace cs = crucible::concurrent::scheduler;

    cc::Pool<cs::Fifo> a{cc::CoreCount{1}};
    cc::Pool<cs::Fifo> b{a};
    (void)b;
    return 0;
}
