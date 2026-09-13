#pragma once

// The whole effect-row machinery in one include.  A translation unit
// that needs only the capability tags takes the atom catalog on its own
// instead, which is most of the hot path, and pays a fraction of the
// parse cost.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/ComputationGraded.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/EffectRowLattice.h>
#include <crucible/effects/EffectRowProjection.h>
#include <crucible/effects/OsUniverse.h>
#include <crucible/effects/Resources.h>
