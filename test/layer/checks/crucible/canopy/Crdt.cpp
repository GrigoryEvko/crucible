// The compile-time checks of crucible/canopy/Crdt.h.

#include <crucible/canopy/Crdt.h>

namespace crucible::canopy {

static_assert(Crdt<GSet<int, 8>>);
static_assert(Crdt<OrSet<int, std::uint64_t, 8>>);
static_assert(Crdt<LwwRegister<int, HlcTimestamp>>);
static_assert(Crdt<GCounter<4>>);
static_assert(Crdt<PNCounter<4>>);
static_assert(Crdt<MVRegister<int, 4, 4>>);
static_assert(Crdt<RgaList<int, std::uint64_t, 8>>);

}  // namespace crucible::canopy
