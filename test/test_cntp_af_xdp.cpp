#include <crucible/cntp/AfXdp.h>

#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>   // FIXY-V-172: std::move for rx_frame laundering

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

    // FIXY-V-172 — RX frames are source::External-tagged (untrusted wire
    // data); the laundering boundary yields source::Sanitized.  Both are
    // zero-cost phantom newtypes over packet_view.
    static_assert(std::is_same_v<
        decltype(socket)::rx_frame,
        saf::Tagged<decltype(socket)::packet_view, saf::source::External>>);
    static_assert(std::is_same_v<
        decltype(socket)::sanitized_frame,
        saf::Tagged<decltype(socket)::packet_view, saf::source::Sanitized>>);
    static_assert(sizeof(decltype(socket)::rx_frame) ==
                  sizeof(decltype(socket)::packet_view));

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
    // FIXY-V-172: rx is Tagged<packet_view, source::External> — untrusted
    // wire data.  Launder it through the single sanitize boundary before
    // reading any bytes; only the Sanitized result is safe to consume.
    auto clean = decltype(socket)::sanitize_rx_frame(std::move(*rx));
    assert(clean.has_value());
    assert(clean->value().size() == 96);
    assert(socket.rx_pending() == 0);

    std::printf("  test_socket_substrate_rings: PASSED\n");
}

// fixy-A5-023 HS14 fixture: runtime sentinel that explicitly proves the
// "kernel is not in the loop" contract.  Three "kernel-NOT-mediated"
// assertions, each of which would FAIL on a real AF_XDP socket where the
// kernel drives the rings:
//
//   (a) Fresh socket → rx ring empty (no XDP_REDIRECT feed).
//   (b) enqueue_tx succeeds → completion ring stays empty (no kernel TX
//       drain → completion-post transition).
//   (c) stage_rx_descriptor is the ONLY path to populate the rx ring
//       (test-only injection; real socket would have kernel post these).
//
// When a backend author wires the real AF_XDP lifecycle (open socket,
// mmap rings, bind XDP_REDIRECT), they MUST:
//   (1) flip cntp::kernel_rings_shared from false to true,
//   (2) replace this fixture's "kernel-NOT-mediated" assertions with
//       live-NIC fixtures that exercise the kernel-XDP_REDIRECT → rx and
//       tx-ring → kernel → completion-ring transitions,
//   (3) re-validate the AddressOfFrame bound-check against a real mmap'd
//       UMEM region; the in-process std::bit_cast<uintptr_t> path
//       (line ~344, the fixy-A5-028 / FIXY-U-082 fix) continues to work
//       byte-for-byte against the kernel-shared UMEM.
// Anything short of all three is caught by either the static_assert
// or this runtime sentinel.
void test_rings_are_in_process_only() {
    // (1) Marker invariant: the honesty trait MUST be false until the
    // FIXY-U-087 sweep flips it.
    assert(cntp::kernel_rings_shared == false);

    effects::ColdInitCtx init{};
    auto socket = cntp::mint_af_xdp_socket<131'072, 2'048, 64, 64, 64, 64>(
        init, config());

    // (2a) "kernel-NOT-mediated" claim #1: a freshly-minted socket has
    // ZERO RX descriptors AND ZERO completions because nothing feeds
    // them from kernel space.  A real AF_XDP socket bound via
    // XDP_REDIRECT would already be receiving packets at this point.
    assert(socket.rx_pending() == 0);
    assert(socket.completions_pending() == 0);
    assert(socket.poll() == 0);
    auto first_dequeue = socket.dequeue_rx();
    assert(!first_dequeue.has_value());

    // (2b) "kernel-NOT-mediated" claim #2: enqueue_tx posts to the
    // in-process tx ring; in a real AF_XDP socket the kernel would
    // consume the descriptor and post to the completion ring after
    // the NIC TX'd.  Here, the completion ring stays empty even after
    // a successful enqueue — proves no kernel side-loop.
    auto packet = socket.alloc_tx_buffer(128);
    assert(packet.has_value());
    packet->front() = std::byte{0xCA};
    assert(socket.enqueue_tx(*packet).has_value());
    assert(socket.tx_pending() == 1);
    // KERNEL-IS-NOT-IN-THE-LOOP witness:
    assert(socket.completions_pending() == 0);
    // …and a second poll() still reports only in-process accounting.
    assert(socket.poll() == 0);  // 0 rx + 0 completion; tx is NOT in poll()

    // (2c) "kernel-NOT-mediated" claim #3: stage_rx_descriptor is the
    // ONLY path to populate the rx ring while the socket is a façade.
    // Verify that nothing implicit (timer? thread? kernel?) appears
    // between two distinct polls.
    auto second_dequeue = socket.dequeue_rx();
    assert(!second_dequeue.has_value());  // STILL empty without inject

    // Now inject via the test helper and prove the ring picks it up.
    // In a real kernel-mediated socket, packets would arrive via the
    // XDP_REDIRECT bpf-prog → rx ring path, not via this helper.
    assert(socket.stage_rx_descriptor(5, 64));
    assert(socket.rx_pending() == 1);
    auto staged = socket.dequeue_rx();
    assert(staged.has_value());
    auto staged_clean = decltype(socket)::sanitize_rx_frame(std::move(*staged));
    assert(staged_clean.has_value());
    assert(staged_clean->value().size() == 64);
    assert(socket.rx_pending() == 0);

    // (2d) UMEM ownership remains LINEAR — the `Linear<AlignedBuffer>`
    // is not silently shared with anything.  Compile-time proof: the
    // socket is non-copyable AND non-movable (Pinned + Linear umem).
    static_assert(!std::copy_constructible<decltype(socket)>);
    static_assert(!std::move_constructible<decltype(socket)>);

    std::printf("  test_rings_are_in_process_only: PASSED\n");
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

    // fixy-A5-023: compile-time honesty-marker witnesses.  These fire
    // at translation time if a backend author flips the trait without
    // updating the test discipline (or vice-versa).
    static_assert(!cntp::kernel_rings_shared,
        "fixy-A5-023: AfXdpSocket is a documented façade — rings are "
        "in-process, NOT shared with the kernel.  Flipping "
        "kernel_rings_shared to true requires: (a) live "
        "socket(AF_XDP)+setsockopt+mmap+bind lifecycle, (b) Ring<> "
        "replaced with kernel-shared producer/consumer indices against "
        "the mmap'd ring, (c) GAPS-130 XDP_REDIRECT attachment, "
        "(d) updated test_rings_are_in_process_only.");
    static_assert(std::is_same_v<decltype(cntp::kernel_rings_shared),
                                 const bool>,
        "fixy-A5-023: honesty trait must be a compile-time bool");

    std::printf("test_cntp_af_xdp:\n");
    test_admission();
    test_socket_substrate_rings();
    test_rings_are_in_process_only();
    std::printf("test_cntp_af_xdp: all PASSED\n");
    return 0;
}
