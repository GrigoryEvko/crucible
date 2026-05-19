#pragma once

// GAPS-131 substrate slice. CNT-P AF_XDP typed configuration and in-process
// ring model.
//
// This header deliberately does not open AF_XDP sockets, mmap UMEM into the
// kernel, attach XDP_REDIRECT, or poll a live NIC. GAPS-130 owns XDP attachment
// and the full GAPS-131 closure must add the Linux xsk/umem lifecycle. This
// slice pins the safety shape first: power-of-two frame/ring dimensions, linear
// UMEM ownership, source::AfXdp configuration provenance, and borrowed packet
// views tied to the socket type.
//
// fixy-A5-023 honesty marker.  The header is split into TWO tiers; flipping
// `kernel_rings_shared` from false to true is the contract a backend author
// MUST satisfy before claiming the socket is kernel-mediated:
//
//   LIVE TIER (in-process — verified by test_cntp_af_xdp):
//     • admit_af_xdp_{ifindex,queue_id,frame_size,frame_count,ring_entries}
//     • mint_af_xdp_config / mint_af_xdp_socket / AfXdpSocket::mint
//     • AfXdpSocket::alloc_tx_buffer       (in-process UMEM bump alloc)
//     • AfXdpSocket::enqueue_tx            (in-process tx Ring push + addr
//                                           bounds via std::bit_cast — the
//                                           FIXY-U-082 / fixy-A5-028 fix)
//     • AfXdpSocket::dequeue_rx            (in-process rx Ring pop)
//     • AfXdpSocket::stage_rx_descriptor   (test-only RX injection)
//     • AfXdpSocket::poll                  (in-process ring accounting)
//
//   STUB TIER (no kernel mediation):
//     • No socket(AF_XDP, SOCK_RAW, 0) syscall.
//     • No setsockopt(XDP_UMEM_REG / XDP_FILL_RING / XDP_TX_RING /
//                     XDP_RX_RING / XDP_COMPLETION_RING).
//     • No mmap(MAP_SHARED, fd, XDP_UMEM_PGOFF_* / XDP_PGOFF_*) of UMEM
//       or any of the four rings into kernel space.
//     • No bind(fd, sockaddr_xdp{queue_id, ifindex}) attachment.
//     • No XDP_REDIRECT bpf-map population — GAPS-130 owns that.
//     • No real NIC poll — `poll()` returns in-process ring sizes only.
//     • No kernel feed of the rx ring — only `stage_rx_descriptor` posts.
//     • No kernel drain of the tx ring → completion ring transition; the
//       completion ring stays empty even after `enqueue_tx` succeeds.
//
// What flipping `kernel_rings_shared = true` requires:
//   1. open the AF_XDP socket and bind to (ifindex, queue_id, mode).
//   2. setsockopt(XDP_UMEM_REG) with the AlignedBuffer-backed UMEM region;
//      the linear ownership of `umem_` MUST be preserved — the kernel only
//      receives a borrowed mapping, NOT consume the Linear<AlignedBuffer>.
//   3. setsockopt + mmap the four rings; replace the in-process
//      `Ring<Capacity>` arrays with kernel-shared producer/consumer indices
//      against the mmap'd region.  The Descriptor layout MUST match
//      `struct xdp_desc` (24 bytes: addr/len/options).
//   4. bind(fd, sockaddr_xdp) and confirm the XDP_REDIRECT bpf-prog is
//      attached (GAPS-130 closure).
//   5. `poll()` must call poll(2) on the AF_XDP fd and reconcile the
//      kernel-published indices, not just sum in-process ring sizes.
//   6. `stage_rx_descriptor` becomes a NO-OP or test-only path; production
//      RX flows through the kernel-XDP_REDIRECT → rx ring path.
//   7. FIXY-U-087 sweep flips `kernel_rings_shared` and updates
//      test_cntp_af_xdp to exercise the live kernel-mediated path.
//
// Until then: this header refuses to fabricate kernel mediation.  The trait
// is a compile-time honesty marker — call-site `static_assert`s and runtime
// sentinel tests pin the in-process-only contract so a partial flip (e.g.,
// mmap'ing UMEM without binding the socket) is caught at translation time,
// not at production-packet-loss time.

#include <crucible/Platform.h>
#include <crucible/cntp/Pacing.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/safety/AlignedBuffer.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Pinned.h>
#include <crucible/safety/Pre.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <bit>          // FIXY-U-082: std::bit_cast for pointer↔uintptr_t
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace crucible::cntp {

// fixy-A5-023 honesty marker.  See the file-level doc-block for the precise
// live-vs-stub tier split.  This `inline constexpr` is the machine-readable
// form of that contract:
//
//   while kernel_rings_shared == false:
//     • No syscalls land — no socket(AF_XDP), no mmap, no bind.
//     • Rings are in-process std::array; rx ring stays empty until
//       stage_rx_descriptor injects entries.
//     • Completion ring is permanently empty (no kernel TX drain).
//     • UMEM is private to this process; no kernel mapping.
//
//   when kernel_rings_shared flips to true:
//     • Production AF_XDP lifecycle is wired (socket/setsockopt/mmap/bind).
//     • The runtime sentinel test_rings_are_in_process_only must be updated
//       in lockstep to exercise the kernel-mediated path; the in-process
//       assertions below would become false-positives masking real packet loss.
//
// FIXY-U-087 is the cross-substrate sweep that flips this for every façade'd
// transport surface (MtlsTransport / NicConfig / RoceConfig / SrIov / Tcam /
// AfXdp).  Until that sweep, this header refuses to fabricate kernel mediation.
inline constexpr bool kernel_rings_shared = false;

enum class AfXdpMode : std::uint8_t {
    Copy = 0,
    ZeroCopy = 1,
};

enum class AfXdpError : std::uint8_t {
    InvalidInterfaceName,
    InvalidIfIndex,
    InvalidQueueId,
    InvalidFrameSize,
    InvalidFrameCount,
    InvalidRingSize,
    InvalidUmemShape,
    PacketTooLarge,
    InvalidFrameAddress,
    TxRingFull,
    RxRingEmpty,
    CompletionRingEmpty,
    XdpRedirectMissing,
};

[[nodiscard]] std::string_view af_xdp_mode_name(AfXdpMode mode) noexcept;
[[nodiscard]] std::string_view af_xdp_error_name(AfXdpError error) noexcept;

using AfXdpIfIndex = safety::Positive<std::uint32_t>;
using AfXdpQueueId =
    safety::Bounded<std::uint32_t{0}, std::uint32_t{65'535}, std::uint32_t>;
using AfXdpFrameSize = safety::PowerOfTwo<std::uint32_t>;
using AfXdpFrameCount = safety::PowerOfTwo<std::uint32_t>;
using AfXdpRingEntries = safety::PowerOfTwo<std::uint32_t>;

struct AfXdpConfig {
    NicInterfaceName interface{};
    AfXdpIfIndex ifindex{std::uint32_t{1}};
    AfXdpQueueId queue_id{std::uint32_t{0}, typename AfXdpQueueId::Trusted{}};
    AfXdpFrameSize frame_size{std::uint32_t{2'048}};
    AfXdpFrameCount frame_count{std::uint32_t{2'048}};
    AfXdpRingEntries fill_ring_size{std::uint32_t{2'048}};
    AfXdpRingEntries completion_ring_size{std::uint32_t{2'048}};
    AfXdpRingEntries rx_ring_size{std::uint32_t{2'048}};
    AfXdpRingEntries tx_ring_size{std::uint32_t{2'048}};
    AfXdpMode mode = AfXdpMode::ZeroCopy;
    bool require_xdp_redirect = true;
};

using DeclaredAfXdpConfig =
    safety::Tagged<AfXdpConfig, safety::source::AfXdp>;

[[nodiscard]] constexpr std::expected<AfXdpIfIndex, AfXdpError>
admit_af_xdp_ifindex(std::uint32_t ifindex) noexcept {
    if (ifindex == 0) {
        return std::unexpected(AfXdpError::InvalidIfIndex);
    }
    return AfXdpIfIndex{ifindex, typename AfXdpIfIndex::Trusted{}};
}

[[nodiscard]] constexpr std::expected<AfXdpQueueId, AfXdpError>
admit_af_xdp_queue_id(std::uint32_t queue_id) noexcept {
    if (queue_id > 65'535u) {
        return std::unexpected(AfXdpError::InvalidQueueId);
    }
    return AfXdpQueueId{queue_id, typename AfXdpQueueId::Trusted{}};
}

[[nodiscard]] constexpr std::expected<AfXdpFrameSize, AfXdpError>
admit_af_xdp_frame_size(std::uint32_t bytes) noexcept {
    if (!safety::power_of_two(bytes) || bytes < 1'024u || bytes > 16'384u) {
        return std::unexpected(AfXdpError::InvalidFrameSize);
    }
    return AfXdpFrameSize{bytes, typename AfXdpFrameSize::Trusted{}};
}

[[nodiscard]] constexpr std::expected<AfXdpFrameCount, AfXdpError>
admit_af_xdp_frame_count(std::uint32_t frames) noexcept {
    if (!safety::power_of_two(frames) || frames < 64u) {
        return std::unexpected(AfXdpError::InvalidFrameCount);
    }
    return AfXdpFrameCount{frames, typename AfXdpFrameCount::Trusted{}};
}

[[nodiscard]] constexpr std::expected<AfXdpRingEntries, AfXdpError>
admit_af_xdp_ring_entries(std::uint32_t entries) noexcept {
    if (!safety::power_of_two(entries) || entries < 64u) {
        return std::unexpected(AfXdpError::InvalidRingSize);
    }
    return AfXdpRingEntries{entries, typename AfXdpRingEntries::Trusted{}};
}

[[nodiscard]] constexpr std::expected<DeclaredAfXdpConfig, AfXdpError>
mint_af_xdp_config(NicInterfaceName interface,
                   AfXdpIfIndex ifindex,
                   AfXdpQueueId queue_id,
                   AfXdpFrameSize frame_size,
                   AfXdpFrameCount frame_count,
                   AfXdpRingEntries fill_ring_size,
                   AfXdpRingEntries completion_ring_size,
                   AfXdpRingEntries rx_ring_size,
                   AfXdpRingEntries tx_ring_size,
                   AfXdpMode mode = AfXdpMode::ZeroCopy,
                   bool require_xdp_redirect = true) noexcept {
    const std::uint64_t umem_bytes =
        static_cast<std::uint64_t>(frame_size.value()) * frame_count.value();
    if (!safety::power_of_two(umem_bytes)) {
        return std::unexpected(AfXdpError::InvalidUmemShape);
    }
    return DeclaredAfXdpConfig{AfXdpConfig{
        .interface = interface,
        .ifindex = ifindex,
        .queue_id = queue_id,
        .frame_size = frame_size,
        .frame_count = frame_count,
        .fill_ring_size = fill_ring_size,
        .completion_ring_size = completion_ring_size,
        .rx_ring_size = rx_ring_size,
        .tx_ring_size = tx_ring_size,
        .mode = mode,
        .require_xdp_redirect = require_xdp_redirect,
    }};
}

template <std::uint32_t UmemBytes,
          std::uint32_t FrameSize,
          std::uint32_t FillRing,
          std::uint32_t CompletionRing,
          std::uint32_t RxRing,
          std::uint32_t TxRing>
concept AfXdpStaticShape =
       safety::power_of_two(UmemBytes)
    && safety::power_of_two(FrameSize)
    && safety::power_of_two(FillRing)
    && safety::power_of_two(CompletionRing)
    && safety::power_of_two(RxRing)
    && safety::power_of_two(TxRing)
    && FrameSize >= 1'024u
    && FrameSize <= 16'384u
    && (UmemBytes % FrameSize) == 0u
    && (UmemBytes / FrameSize) >= 64u
    && FillRing >= 64u
    && CompletionRing >= 64u
    && RxRing >= 64u
    && TxRing >= 64u;

template <class Ctx>
concept CtxFitsAfXdpMint =
       effects::IsExecCtx<Ctx>
    && effects::row_contains_v<effects::row_type_of_t<Ctx>,
                               effects::Effect::Init>;

template <std::uint32_t UmemBytes,
          std::uint32_t FrameSize,
          std::uint32_t FillRing,
          std::uint32_t CompletionRing,
          std::uint32_t RxRing,
          std::uint32_t TxRing>
[[nodiscard]] constexpr bool
af_xdp_config_matches_static_shape(DeclaredAfXdpConfig config) noexcept {
    AfXdpConfig const& raw = config.value();
    return raw.frame_size.value() == FrameSize
        && raw.frame_count.value() == (UmemBytes / FrameSize)
        && raw.fill_ring_size.value() == FillRing
        && raw.completion_ring_size.value() == CompletionRing
        && raw.rx_ring_size.value() == RxRing
        && raw.tx_ring_size.value() == TxRing;
}

template <std::uint32_t UmemBytes,
          std::uint32_t FrameSize,
          std::uint32_t FillRing = 2'048,
          std::uint32_t CompletionRing = 2'048,
          std::uint32_t RxRing = 2'048,
          std::uint32_t TxRing = 2'048>
    requires AfXdpStaticShape<UmemBytes, FrameSize, FillRing,
                              CompletionRing, RxRing, TxRing>
class AfXdpSocket
    : public safety::Pinned<AfXdpSocket<UmemBytes, FrameSize, FillRing,
                                        CompletionRing, RxRing, TxRing>> {
public:
    using byte_type = std::byte;
    using umem_type = safety::AlignedBuffer<byte_type, 4'096>;
    using linear_umem_type = safety::Linear<umem_type>;
    using packet_view =
        safety::Borrowed<byte_type, AfXdpSocket>;

    struct Descriptor {
        std::uint32_t frame_id = 0;
        std::uint32_t length = 0;
    };

    static constexpr std::uint32_t umem_bytes = UmemBytes;
    static constexpr std::uint32_t frame_size = FrameSize;
    static constexpr std::uint32_t frame_count = UmemBytes / FrameSize;
    static constexpr std::uint32_t fill_ring_entries = FillRing;
    static constexpr std::uint32_t completion_ring_entries = CompletionRing;
    static constexpr std::uint32_t rx_ring_entries = RxRing;
    static constexpr std::uint32_t tx_ring_entries = TxRing;

private:
    template <std::uint32_t Capacity>
    class Ring {
        static_assert(safety::power_of_two(Capacity));

        std::array<Descriptor, Capacity> slots_{};
        std::uint32_t head_ = 0;
        std::uint32_t tail_ = 0;
        std::uint32_t size_ = 0;

    public:
        [[nodiscard]] constexpr bool empty() const noexcept {
            return size_ == 0;
        }

        [[nodiscard]] constexpr bool full() const noexcept {
            return size_ == Capacity;
        }

        [[nodiscard]] constexpr std::uint32_t size() const noexcept {
            return size_;
        }

        [[nodiscard]] constexpr bool push(Descriptor desc) noexcept {
            if (full()) {
                return false;
            }
            slots_[tail_ & (Capacity - 1u)] = desc;
            ++tail_;
            ++size_;
            return true;
        }

        [[nodiscard]] constexpr std::optional<Descriptor> pop() noexcept {
            if (empty()) {
                return std::nullopt;
            }
            Descriptor desc = slots_[head_ & (Capacity - 1u)];
            ++head_;
            --size_;
            return desc;
        }
    };

    DeclaredAfXdpConfig config_;
    linear_umem_type umem_;
    Ring<FillRing> fill_{};
    Ring<CompletionRing> completion_{};
    Ring<RxRing> rx_{};
    Ring<TxRing> tx_{};
    std::uint32_t next_frame_ = 0;

    explicit AfXdpSocket(DeclaredAfXdpConfig config)
        : config_{config},
          umem_{umem_type::allocate(UmemBytes)} {}

    [[nodiscard]] CRUCIBLE_HOT packet_view view(Descriptor desc) noexcept {
        byte_type* base = umem_.peek_mut().data();
        return packet_view{base + static_cast<std::size_t>(desc.frame_id) *
                                      FrameSize,
                           desc.length};
    }

public:
    template <class Ctx>
        requires CtxFitsAfXdpMint<Ctx>
    [[nodiscard]] static AfXdpSocket mint(Ctx const&,
                                          DeclaredAfXdpConfig config) {
        CRUCIBLE_PRE((af_xdp_config_matches_static_shape<
            UmemBytes, FrameSize, FillRing, CompletionRing, RxRing, TxRing>(
                config)));
        return AfXdpSocket{config};
    }

    [[nodiscard]] constexpr AfXdpConfig const& config() const noexcept {
        return config_.value();
    }

    [[nodiscard]] constexpr std::uint32_t tx_pending() const noexcept {
        return tx_.size();
    }

    [[nodiscard]] constexpr std::uint32_t rx_pending() const noexcept {
        return rx_.size();
    }

    [[nodiscard]] constexpr std::uint32_t completions_pending() const noexcept {
        return completion_.size();
    }

    [[nodiscard]] CRUCIBLE_HOT std::optional<packet_view>
    alloc_tx_buffer(std::uint32_t length) noexcept {
        if (length == 0 || length > FrameSize || next_frame_ >= frame_count) {
            return std::nullopt;
        }
        const Descriptor desc{.frame_id = next_frame_, .length = length};
        ++next_frame_;
        return view(desc);
    }

    [[nodiscard]] CRUCIBLE_HOT std::expected<void, AfXdpError>
    enqueue_tx(packet_view packet) noexcept {
        if (packet.empty() || packet.size() > FrameSize) {
            return std::unexpected(AfXdpError::PacketTooLarge);
        }
        byte_type* base = umem_.peek_mut().data();
        // FIXY-U-082 / fixy-A5-028: std::bit_cast is the C++26 idiom for
        // pointer↔uintptr_t value reinterpretation; same machine code as
        // reinterpret_cast but constexpr-friendly + no strict-aliasing risk.
        const auto base_addr = std::bit_cast<std::uintptr_t>(base);
        const auto packet_addr = std::bit_cast<std::uintptr_t>(packet.data());
        if (packet_addr < base_addr) {
            return std::unexpected(AfXdpError::InvalidFrameAddress);
        }
        const auto offset = packet_addr - base_addr;
        if (offset >= UmemBytes || (offset % FrameSize) != 0u) {
            return std::unexpected(AfXdpError::InvalidFrameAddress);
        }
        const Descriptor desc{
            .frame_id = static_cast<std::uint32_t>(offset / FrameSize),
            .length = static_cast<std::uint32_t>(packet.size()),
        };
        if (!tx_.push(desc)) {
            return std::unexpected(AfXdpError::TxRingFull);
        }
        return {};
    }

    [[nodiscard]] CRUCIBLE_HOT std::optional<packet_view>
    dequeue_rx() noexcept {
        auto desc = rx_.pop();
        if (!desc.has_value()) {
            return std::nullopt;
        }
        return view(*desc);
    }

    [[nodiscard]] CRUCIBLE_HOT std::uint32_t poll() noexcept {
        return rx_.size() + completion_.size();
    }

    [[nodiscard]] CRUCIBLE_HOT bool stage_rx_descriptor(
        std::uint32_t frame_id,
        std::uint32_t length) noexcept {
        if (frame_id >= frame_count || length == 0 || length > FrameSize) {
            return false;
        }
        return rx_.push(Descriptor{.frame_id = frame_id, .length = length});
    }
};

template <std::uint32_t UmemBytes,
          std::uint32_t FrameSize,
          std::uint32_t FillRing = 2'048,
          std::uint32_t CompletionRing = 2'048,
          std::uint32_t RxRing = 2'048,
          std::uint32_t TxRing = 2'048,
          class Ctx>
    requires AfXdpStaticShape<UmemBytes, FrameSize, FillRing,
                              CompletionRing, RxRing, TxRing>
          && CtxFitsAfXdpMint<Ctx>
[[nodiscard]] AfXdpSocket<UmemBytes, FrameSize, FillRing,
                          CompletionRing, RxRing, TxRing>
mint_af_xdp_socket(Ctx const& ctx, DeclaredAfXdpConfig config) {
    return AfXdpSocket<UmemBytes, FrameSize, FillRing,
                       CompletionRing, RxRing, TxRing>::mint(ctx, config);
}

static_assert(sizeof(AfXdpIfIndex) == sizeof(std::uint32_t));
static_assert(sizeof(AfXdpQueueId) == sizeof(std::uint32_t));
static_assert(sizeof(AfXdpFrameSize) == sizeof(std::uint32_t));
static_assert(sizeof(AfXdpFrameCount) == sizeof(std::uint32_t));
static_assert(sizeof(AfXdpRingEntries) == sizeof(std::uint32_t));
static_assert(sizeof(DeclaredAfXdpConfig) == sizeof(AfXdpConfig));
static_assert(std::is_trivially_copyable_v<AfXdpConfig>);
static_assert(AfXdpStaticShape<131'072, 2'048, 64, 64, 64, 64>);
static_assert(!AfXdpStaticShape<131'072, 1'500, 64, 64, 64, 64>);

}  // namespace crucible::cntp
