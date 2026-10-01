// The compile-time checks of crucible/cntp/Pacing.h.

#include <crucible/cntp/Pacing.h>

namespace crucible::cntp {

static_assert(sizeof(PositivePacingRate) == sizeof(std::uint64_t));
static_assert(std::is_trivially_copyable_v<NicInterfaceName>);
// The refined fq fields keep the config from being trivially copyable, so
// no byte copy builds one; the copy and the destructor stay trivial.
static_assert(std::is_trivially_copy_constructible_v<QdiscConfig> && std::is_trivially_destructible_v<QdiscConfig>);
// Aggregate initialization would reach the private length again, this
// time through `NicInterfaceName{bytes, 200}` rather than assignment.
// from() must stay the only writer of the length.
static_assert(!std::is_aggregate_v<NicInterfaceName>,
              "NicInterfaceName must not be an aggregate: from() is the only path that may set the length");

}  // namespace crucible::cntp
