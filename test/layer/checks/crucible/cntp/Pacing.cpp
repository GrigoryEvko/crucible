// The compile-time checks of crucible/cntp/Pacing.h.

#include <crucible/cntp/Pacing.h>

namespace crucible::cntp {

static_assert(sizeof(PositivePacingRate) == sizeof(std::uint64_t));
// The FixedText of the name is not trivially copyable, so no byte copy
// builds a name.  The copy and the destructor stay trivial.
static_assert(std::is_trivially_copy_constructible_v<NicInterfaceName>
              && std::is_trivially_destructible_v<NicInterfaceName>);
// The refined fq fields keep the config from being trivially copyable, so
// no byte copy builds one; the copy and the destructor stay trivial.
static_assert(std::is_trivially_copy_constructible_v<QdiscConfig> && std::is_trivially_destructible_v<QdiscConfig>);
// Aggregate initialization would set the characters with no check.  from()
// must stay the only writer of the characters.
static_assert(!std::is_aggregate_v<NicInterfaceName>,
              "NicInterfaceName must not be an aggregate: from() is the only path that may set the characters");
// The path of the longest leaf under the directory of the longest name fits
// its capacity.
static_assert(detail::InterfacePath::capacity
              == detail::sysfs_net_root.size() + NicInterfaceName::max_bytes + detail::interface_leaf_bytes);

}  // namespace crucible::cntp
