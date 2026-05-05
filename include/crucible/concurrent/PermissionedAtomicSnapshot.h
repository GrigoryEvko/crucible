#pragma once

// Compatibility name for the AtomicSnapshot permissioned SWMR surface.
// PermissionedSnapshot<T, UserTag> is the shipped implementation; this
// alias keeps call sites that name the underlying primitive explicit.

#include <crucible/concurrent/PermissionedSnapshot.h>

namespace crucible::concurrent {

template <SnapshotValue T, typename UserTag = void>
using PermissionedAtomicSnapshot = PermissionedSnapshot<T, UserTag>;

}  // namespace crucible::concurrent

