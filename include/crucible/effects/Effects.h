#pragma once

// The whole effect-row machinery in one include.  A translation unit
// that needs only the capability tags takes the atom catalog on its own
// instead, which is most of the hot path, and pays a fraction of the
// parse cost.

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_Computation.h>
#include <crucible/effects/_ComputationGraded.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_EffectRowLattice.h>
#include <crucible/effects/EffectRowProjection.h>
#include <crucible/effects/OsUniverse.h>
#include <crucible/effects/Resources.h>
