#pragma once

// Aggregates the value-level wrappers, the ones that decorate a value
// with a compile-time predicate, classification, linearity, typestate
// or mutation discipline.  Ownership, resource handles, protocols,
// cross-substrate composition, the graded foundation and the effect
// rows each have an umbrella of their own.

#include <crucible/safety/AllocClass.h>
#include <crucible/safety/Bits.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/IsBits.h>
#include <crucible/safety/IsBorrowed.h>
#include <crucible/safety/IsBorrowedRef.h>
#include <crucible/safety/Budgeted.h>
#include <crucible/safety/Checked.h>
#include <crucible/safety/CipherTier.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/EpochVersioned.h>
#include <crucible/safety/FixedArray.h>
#include <crucible/safety/NumaPlacement.h>
#include <crucible/safety/RecipeSpec.h>
#include <crucible/safety/Consistency.h>
#include <crucible/safety/Crash.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/HotPath.h>
#include <crucible/safety/MemOrder.h>
#include <crucible/safety/Progress.h>
#include <crucible/safety/ResidencyHeat.h>
#include <crucible/safety/Vendor.h>
#include <crucible/safety/Wait.h>
#include <crucible/safety/_Affine.h>
#include <crucible/safety/_Diagnostic.h>
#include <crucible/safety/DimensionTraits.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/IsLinear.h>
#include <crucible/safety/Machine.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/NotInherited.h>
#include <crucible/safety/NumericalTier.h>
#include <crucible/safety/OpaqueLifetime.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/_Pinned.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_IsRefined.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/_SealedRefined.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/_IsSecret.h>
#include <crucible/safety/Simd.h>
#include <crucible/safety/Saturated.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/IsStale.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/_IsTagged.h>
#include <crucible/safety/TimeOrdered.h>
#include <crucible/safety/Witness.h>
#include <crucible/safety/JoinPolicy.h>
#include <crucible/safety/IsJoinPolicy.h>
#include <crucible/safety/ThreadLocalRef.h>
#include <crucible/safety/Workload.h>
