// The compile-time checks of crucible/canopy/Hlc.h.

#include <crucible/canopy/Hlc.h>

namespace crucible::canopy {

static_assert(sizeof(HlcTimestamp) == 16);
static_assert(std::is_trivially_copyable_v<HlcTimestamp>);
static_assert(std::is_trivially_destructible_v<HlcTimestamp>);
static_assert(sizeof(HlcClockTimestamp) == sizeof(HlcTimestamp));
static_assert(std::is_trivially_copyable_v<HlcClockTimestamp>);
static_assert(std::is_trivially_destructible_v<HlcClockTimestamp>);

namespace detail {

static_assert(alignof(AtomicPackedHlcState) >= 16);

}  // namespace detail

static_assert(alignof(Hlc) == 64);
static_assert(sizeof(Hlc) == 64);
static_assert(!std::is_default_constructible_v<Hlc>);
static_assert(!std::is_copy_constructible_v<Hlc>);
static_assert(!std::is_move_constructible_v<Hlc>);

}  // namespace crucible::canopy
