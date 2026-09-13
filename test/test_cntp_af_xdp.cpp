#include <crucible/cntp/AfXdp.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>

namespace cntp = crucible::cntp;
namespace effects = crucible::effects;
namespace saf = crucible::safety;

namespace {

[[nodiscard]] cntp::DeclaredAfXdpConfig config() {
    auto iface = cntp::NicInterfaceName::from("eth0");
    auto ifindex = cntp::admit_af_xdp_ifindex(7);
    auto queue = cntp::admit_af_xdp_queue_id(3);
    auto frame = cntp::admit_af_xdp_frame_size(2048);
    auto frames = cntp::admit_af_xdp_frame_count(64);
    auto ring = cntp::admit_af_xdp_ring_entries(64);

    assert(iface.has_value());
    assert(ifindex.has_value());
    assert(queue.has_value());
    assert(frame.has_value());
    assert(frames.has_value());
    assert(ring.has_value());

    auto cfg = cntp::mint_af_xdp_config(*iface, *ifindex, *queue, *frame, *frames, *ring, *ring, *ring, *ring);
    assert(cfg.has_value());
    return *cfg;
}

void test_admission() {
    assert(cntp::af_xdp_mode_name(cntp::AfXdpMode::ZeroCopy) == std::string_view{"zero_copy"});
    assert(cntp::af_xdp_error_name(cntp::AfXdpError::TxRingFull) == std::string_view{"TxRingFull"});

    assert(!cntp::admit_af_xdp_ifindex(0).has_value());
    assert(!cntp::admit_af_xdp_queue_id(70000).has_value());
    assert(!cntp::admit_af_xdp_frame_size(1500).has_value());
    assert(!cntp::admit_af_xdp_frame_count(63).has_value());
    assert(!cntp::admit_af_xdp_ring_entries(0).has_value());

    std::printf("  test_admission: PASSED\n");
}

void test_socket_substrate_rings() {
    effects::ColdInitCtx init{};
    auto socket = cntp::mint_af_xdp_socket<131072, 2048, 64, 64, 64, 64>(init, config());

    static_assert(decltype(socket)::umem_bytes == 131072);
    static_assert(decltype(socket)::frame_count == 64);
    static_assert(!std::copy_constructible<decltype(socket)>);
    static_assert(!std::move_constructible<decltype(socket)>);

    // A received frame carries an External tag because it is untrusted
    // wire data.  The laundering boundary is the only thing that yields
    // a Sanitized one.  Both tags are zero-cost phantom newtypes.
    static_assert(
        std::is_same_v<decltype(socket)::rx_frame, saf::Tagged<decltype(socket)::packet_view, saf::source::External>>);
    static_assert(std::is_same_v<decltype(socket)::sanitized_frame,
                                 saf::Tagged<decltype(socket)::packet_view, saf::source::Sanitized>>);
    static_assert(sizeof(decltype(socket)::rx_frame) == sizeof(decltype(socket)::packet_view));

    auto oversized = socket.alloc_tx_buffer(4096);
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
    // Reading bytes out of the frame requires laundering it first.  Only
    // the sanitized result is safe to consume.
    auto clean = decltype(socket)::sanitize_rx_frame(std::move(*rx));
    assert(clean.has_value());
    assert(clean->value().size() == 96);
    assert(socket.rx_pending() == 0);

    std::printf("  test_socket_substrate_rings: PASSED\n");
}

// This socket is a façade: its rings live in this process and the
// kernel never touches them.  Every assertion below would fail against a
// real AF_XDP socket, where the kernel feeds the rx ring through an XDP
// redirect and drains the tx ring into the completion ring.  That
// inversion is the point, because absence of kernel activity is what is
// being proved.
void test_rings_are_in_process_only() {
    // The trait stays false for as long as the façade stands.
    assert(cntp::kernel_rings_shared == false);

    effects::ColdInitCtx init{};
    auto socket = cntp::mint_af_xdp_socket<131072, 2048, 64, 64, 64, 64>(init, config());

    // A freshly minted socket carries nothing, because nothing in kernel
    // space feeds it.
    assert(socket.rx_pending() == 0);
    assert(socket.completions_pending() == 0);
    assert(socket.poll() == 0);
    auto first_dequeue = socket.dequeue_rx();
    assert(!first_dequeue.has_value());

    auto packet = socket.alloc_tx_buffer(128);
    assert(packet.has_value());
    packet->front() = std::byte{0xCA};
    assert(socket.enqueue_tx(*packet).has_value());
    assert(socket.tx_pending() == 1);
    // The completion ring stays empty because nothing drains the tx ring
    // on the far side.
    assert(socket.completions_pending() == 0);
    assert(socket.poll() == 0);  // poll counts rx and completions, never tx

    // Staging descriptors is the only way to fill the rx ring, so a
    // second dequeue between two polls still comes back empty.
    auto second_dequeue = socket.dequeue_rx();
    assert(!second_dequeue.has_value());

    // The staging helper stands in for the redirect path a kernel would
    // otherwise drive.
    assert(socket.stage_rx_descriptor(5, 64));
    assert(socket.rx_pending() == 1);
    auto staged = socket.dequeue_rx();
    assert(staged.has_value());
    auto staged_clean = decltype(socket)::sanitize_rx_frame(std::move(*staged));
    assert(staged_clean.has_value());
    assert(staged_clean->value().size() == 64);
    assert(socket.rx_pending() == 0);

    // Being neither copyable nor movable is what keeps the UMEM buffer
    // linearly owned rather than quietly shared.
    static_assert(!std::copy_constructible<decltype(socket)>);
    static_assert(!std::move_constructible<decltype(socket)>);

    std::printf("  test_rings_are_in_process_only: PASSED\n");
}

}  // namespace

int main() {
    static_assert(std::same_as<cntp::DeclaredAfXdpConfig::tag_type, saf::source::AfXdp>);
    static_assert(sizeof(cntp::DeclaredAfXdpConfig) == sizeof(cntp::AfXdpConfig));
    static_assert(cntp::AfXdpStaticShape<131072, 2048, 64, 64, 64, 64>);
    static_assert(!cntp::AfXdpStaticShape<131072, 1500, 64, 64, 64, 64>);
    static_assert(cntp::CtxFitsAfXdpMint<effects::ColdInitCtx>);
    static_assert(!cntp::CtxFitsAfXdpMint<effects::BgDrainCtx>);

    // These fire at translation time if the trait is flipped without the
    // runtime sentinel above being rewritten to match.
    static_assert(!cntp::kernel_rings_shared, "AfXdpSocket is a façade — its rings are in-process and are not "
                                              "shared with the kernel.  Setting kernel_rings_shared to true "
                                              "requires a live socket, setsockopt, mmap and bind lifecycle, "
                                              "ring indices shared with the kernel against the mmap'd region, "
                                              "an XDP redirect attachment, and a rewritten "
                                              "test_rings_are_in_process_only.");
    static_assert(std::is_same_v<decltype(cntp::kernel_rings_shared), const bool>,
                  "kernel_rings_shared must be a compile-time bool");

    std::printf("test_cntp_af_xdp:\n");
    test_admission();
    test_socket_substrate_rings();
    test_rings_are_in_process_only();
    std::printf("test_cntp_af_xdp: all PASSED\n");
    return 0;
}
