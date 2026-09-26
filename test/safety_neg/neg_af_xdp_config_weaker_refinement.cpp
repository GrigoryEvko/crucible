// A frame size refined only as a power of two does not satisfy the kernel
// bound the config door demands, because a power of two may still be too
// small or too large.  The weaker refinement does not convert to the stronger.

#include <crucible/cntp/AfXdp.h>

#include <cstdint>

namespace cntp = crucible::cntp;

int main() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    auto ifindex = cntp::admit_af_xdp_ifindex(7);
    auto queue = cntp::admit_af_xdp_queue_id(3);
    auto frames = cntp::admit_af_xdp_frame_count(64);
    auto ring = cntp::admit_af_xdp_ring_entries(64);
    ::fixy::PowerOfTwo<std::uint32_t> tiny = ::fixy::mint_refined<::fixy::power_of_two>(std::uint32_t{64});
    auto cfg = cntp::mint_af_xdp_config(*iface, *ifindex, *queue, tiny, *frames, *ring, *ring, *ring, *ring);
    return cfg.has_value() ? 0 : 1;
}
