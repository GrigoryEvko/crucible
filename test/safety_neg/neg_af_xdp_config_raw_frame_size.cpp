// The config door takes each shape as a refined value, so a raw frame size
// that never passed admit_af_xdp_frame_size does not convert.

#include <crucible/cntp/AfXdp.h>

#include <cstdint>

namespace cntp = crucible::cntp;

int main() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    auto ifindex = cntp::admit_af_xdp_ifindex(7);
    auto queue = cntp::admit_af_xdp_queue_id(3);
    auto frames = cntp::admit_af_xdp_frame_count(64);
    auto ring = cntp::admit_af_xdp_ring_entries(64);
    auto cfg = cntp::mint_af_xdp_config(*iface, *ifindex, *queue, std::uint32_t{2048}, *frames, *ring, *ring, *ring,
                                        *ring);
    return cfg.has_value() ? 0 : 1;
}
