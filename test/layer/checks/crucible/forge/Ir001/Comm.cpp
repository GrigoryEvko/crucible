// The compile-time checks of crucible/forge/Ir001/Comm.h.

#include <crucible/forge/Ir001/Comm.h>

namespace crucible::forge::ir001 {

// The category ladder reads the kinds as one dense range from zero, so the
// last enumerator must sit at the count minus one.
static_assert(kIr001OpKindCount == std::to_underlying(Ir001OpKind::ScuttlebuttDeltaSend) + 1U);
static_assert(sizeof(Ir001ParticipantCount) == sizeof(std::uint16_t));
static_assert(sizeof(Ir001WireHeader) == 16);
static_assert(std::is_trivially_copyable_v<Ir001WireHeader>);
static_assert(Ir001CollectiveKind<Ir001OpKind::AllReduce>);
static_assert(!Ir001CollectiveKind<Ir001OpKind::SendAsync>);
static_assert(Ir001PointToPointKind<Ir001OpKind::RecvAsync>);
static_assert(!Ir001PointToPointKind<Ir001OpKind::AllGather>);

}  // namespace crucible::forge::ir001
