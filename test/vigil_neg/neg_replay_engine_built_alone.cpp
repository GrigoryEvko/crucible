// Negative-compile test.  This file must fail to compile.
//
// An engine keeps a View of the column of addresses of its pool.  So only the
// context that owns the two builds an engine.  An engine that a caller builds
// alone could read the column of a pool that ended.

#include <crucible/ReplayEngine.h>

int main() {
    crucible::ReplayEngine engine;
    return engine.is_initialized() ? 1 : 0;
}
