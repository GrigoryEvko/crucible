#include <crucible/cntp/AfXdp.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace effects = crucible::effects;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] cntp::DeclaredAfXdpConfig config() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    auto ifindex = cntp::admit_af_xdp_ifindex(7);
    auto queue = cntp::admit_af_xdp_queue_id(3);
    auto frame = cntp::admit_af_xdp_frame_size(2'048);
    auto frames = cntp::admit_af_xdp_frame_count(64);
    auto ring = cntp::admit_af_xdp_ring_entries(64);

    assert(iface.has_value());
    assert(ifindex.has_value());
    assert(queue.has_value());
    assert(frame.has_value());
    assert(frames.has_value());
    assert(ring.has_value());

    auto cfg = cntp::mint_af_xdp_config(*iface, *ifindex, *queue, *frame,
                                        *frames, *ring, *ring, *ring, *ring);
    assert(cfg.has_value());
    return *cfg;
}

void test_admission() {
    assert(cntp::af_xdp_mode_name(cntp::AfXdpMode::ZeroCopy) ==
           std::string_view{"zero_copy"});
    assert(cntp::af_xdp_error_name(cntp::AfXdpError::TxRingFull) ==
           std::string_view{"TxRingFull"});

    assert(!cntp::admit_af_xdp_ifindex(0).has_value());
    assert(!cntp::admit_af_xdp_queue_id(70'000).has_value());
    assert(!cntp::admit_af_xdp_frame_size(1'500).has_value());
    assert(!cntp::admit_af_xdp_frame_count(63).has_value());
    assert(!cntp::admit_af_xdp_ring_entries(0).has_value());

    std::printf("  test_admission: PASSED\n");
}

void test_socket_substrate_rings() {
    effects::ColdInitCtx init{};
    auto socket = cntp::mint_af_xdp_socket<131'072, 2'048, 64, 64, 64, 64>(
        init, config());

    static_assert(decltype(socket)::umem_bytes == 131'072);
    static_assert(decltype(socket)::frame_count == 64);
    static_assert(!std::copy_constructible<decltype(socket)>);
    static_assert(!std::move_constructible<decltype(socket)>);

    auto oversized = socket.alloc_tx_buffer(4'096);
    assert(!oversized.has_value());

    auto packet = socket.alloc_tx_buffer(128);
    assert(packet.has_value());
    packet->front() = std::byte{0xAB};
    assert(socket.enqueue_tx(*packet).has_value());
    assert(socket.tx_pending() == 1);

    std::byte raw[64]{};
    decltype(socket)::packet_view forged{raw};
    auto rejected = socket.enqueue_tx(forged);
    assert(!rejected.has_value());
    assert(rejected.error() == cntp::AfXdpError::InvalidFrameAddress);

    assert(socket.stage_rx_descriptor(1, 96));
    assert(socket.poll() == 1);
    auto rx = socket.dequeue_rx();
    assert(rx.has_value());
    assert(rx->size() == 96);
    assert(socket.rx_pending() == 0);

    std::printf("  test_socket_substrate_rings: PASSED\n");
}

}  // namespace

int main() {
    static_assert(std::same_as<
                  cntp::DeclaredAfXdpConfig::tag_type,
                  saf::source::AfXdp>);
    static_assert(sizeof(cntp::DeclaredAfXdpConfig) ==
                  sizeof(cntp::AfXdpConfig));
    static_assert(cntp::AfXdpStaticShape<131'072, 2'048, 64, 64, 64, 64>);
    static_assert(!cntp::AfXdpStaticShape<131'072, 1'500, 64, 64, 64, 64>);
    static_assert(cntp::CtxFitsAfXdpMint<effects::ColdInitCtx>);
    static_assert(!cntp::CtxFitsAfXdpMint<effects::BgDrainCtx>);

    std::printf("test_cntp_af_xdp:\n");
    test_admission();
    test_socket_substrate_rings();
    std::printf("test_cntp_af_xdp: all PASSED\n");
    return 0;
}
