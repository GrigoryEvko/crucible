// The compile-time checks of crucible/topology/CongestionTelemetry.h.

#include <crucible/topology/CongestionTelemetry.h>

namespace crucible::topology {

static_assert(sizeof(PositiveBandwidthBps) == sizeof(std::uint64_t));
static_assert(sizeof(PositiveMicroseconds) == sizeof(std::uint64_t));
static_assert(sizeof(PositiveWindowBytes) == sizeof(std::uint32_t));
static_assert(sizeof(TcpInfoSnapshot) == sizeof(CongestionSample));
// A refined field keeps no byte route into it, so a sample is not
// trivially copyable.  Its copies stay trivial, so a sample still passes
// by value in registers and copies with no constructor call.
static_assert(std::is_trivially_copy_constructible_v<CongestionSample>
              && std::is_trivially_destructible_v<CongestionSample>);
static_assert(std::is_trivially_copy_constructible_v<TcpInfoSnapshot>
              && std::is_trivially_destructible_v<TcpInfoSnapshot>);

}  // namespace crucible::topology
