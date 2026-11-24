#pragma once

// The tag of a figure that this host measured.  crucible/cog/TargetCaps.h
// and crucible/topology/TopologyEdge.h both name it, and this header depends
// only on fixy, so TopologyEdge.h names it with no include of a header of
// the cog family.

#include <fixy/Tagged.h>
#include <fixy/Tags.h>

namespace crucible::cog {

// A figure measured on this host rather than read from the vendor.
template <typename T>
using CalibratedValue = ::fixy::Tagged<T, ::fixy::tags::source::Calibrated>;

}  // namespace crucible::cog
