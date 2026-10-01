#pragma once

#include <crucible/cntp/Pacing.h>
#include <fixy/Borrowed.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <foundation/AlignedBuffer.h>
#include <foundation/Pinned.h>
#include <foundation/Platform.h>
#include <foundation/contracts/Pre.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::cntp {

// True only when the four rings are shared with the kernel through a bound
// AF_XDP socket.  While it is false this header opens no socket, registers no
// UMEM, mmaps no ring and attaches no XDP program: the rings are
// process-private arrays, the rx ring fills only from stage_rx_descriptor, and
// the completion ring never drains because nothing drives it.  A consumer must
// not read an empty completion ring as evidence of a kernel TX drain.
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

// The kernel spelling of each mode, which is what a log line and an ethtool
// reading name.  An error has no such spelling, so its name is its
// enumerator's identifier, read by foundation::reflect::enum_name.
[[nodiscard]] constexpr std::string_view af_xdp_mode_name(AfXdpMode mode) noexcept {
    switch (mode) {
        case AfXdpMode::Copy:
            return "copy";
        case AfXdpMode::ZeroCopy:
            return "zero_copy";
        default:
            return "unknown";
    }
}

// The shapes the kernel accepts, stated once.  The refined types below,
// the admission doors and the static shape of a socket all read these
// three predicates, so a bound cannot change in one place and not the
// others.
inline constexpr auto af_xdp_queue_id_range = ::fixy::in_range<std::uint32_t{0}, std::uint32_t{65535}>;
inline constexpr auto af_xdp_frame_bytes = ::fixy::all_of<::fixy::power_of_two, ::fixy::in_range<1024u, 16384u>>;
inline constexpr auto af_xdp_ring_depth = ::fixy::all_of<::fixy::power_of_two, ::fixy::bounded_below<64u>>;

using AfXdpIfIndex = ::fixy::Positive<std::uint32_t>;
using AfXdpQueueId = ::fixy::Refined<af_xdp_queue_id_range, std::uint32_t>;
using AfXdpFrameSize = ::fixy::Refined<af_xdp_frame_bytes, std::uint32_t>;
using AfXdpFrameCount = ::fixy::Refined<af_xdp_ring_depth, std::uint32_t>;
using AfXdpRingEntries = ::fixy::Refined<af_xdp_ring_depth, std::uint32_t>;

struct AfXdpConfig {
    NicInterfaceName interface{};
    AfXdpIfIndex ifindex = ::fixy::mint_refined<::fixy::positive>(std::uint32_t{1});
    AfXdpQueueId queue_id = ::fixy::mint_refined<af_xdp_queue_id_range>(std::uint32_t{0});
    AfXdpFrameSize frame_size = ::fixy::mint_refined<af_xdp_frame_bytes>(std::uint32_t{2048});
    AfXdpFrameCount frame_count = ::fixy::mint_refined<af_xdp_ring_depth>(std::uint32_t{2048});
    AfXdpRingEntries fill_ring_size = ::fixy::mint_refined<af_xdp_ring_depth>(std::uint32_t{2048});
    AfXdpRingEntries completion_ring_size = ::fixy::mint_refined<af_xdp_ring_depth>(std::uint32_t{2048});
    AfXdpRingEntries rx_ring_size = ::fixy::mint_refined<af_xdp_ring_depth>(std::uint32_t{2048});
    AfXdpRingEntries tx_ring_size = ::fixy::mint_refined<af_xdp_ring_depth>(std::uint32_t{2048});
    AfXdpMode mode = AfXdpMode::ZeroCopy;
    bool require_xdp_redirect = true;
};

using DeclaredAfXdpConfig = ::fixy::Tagged<AfXdpConfig, ::fixy::tags::source::AfXdp>;

// Each admission refuses a value the predicate rejects with the error it
// names, and mints the refined value for one it admits.

[[nodiscard]] constexpr std::expected<AfXdpIfIndex, AfXdpError> admit_af_xdp_ifindex(std::uint32_t ifindex) noexcept {
    return ::fixy::admit_refined<::fixy::positive>(ifindex, AfXdpError::InvalidIfIndex);
}

[[nodiscard]] constexpr std::expected<AfXdpQueueId, AfXdpError> admit_af_xdp_queue_id(std::uint32_t queue_id) noexcept {
    return ::fixy::admit_refined<af_xdp_queue_id_range>(queue_id, AfXdpError::InvalidQueueId);
}

[[nodiscard]] constexpr std::expected<AfXdpFrameSize, AfXdpError>
admit_af_xdp_frame_size(std::uint32_t bytes) noexcept {
    return ::fixy::admit_refined<af_xdp_frame_bytes>(bytes, AfXdpError::InvalidFrameSize);
}

[[nodiscard]] constexpr std::expected<AfXdpFrameCount, AfXdpError>
admit_af_xdp_frame_count(std::uint32_t frames) noexcept {
    return ::fixy::admit_refined<af_xdp_ring_depth>(frames, AfXdpError::InvalidFrameCount);
}

[[nodiscard]] constexpr std::expected<AfXdpRingEntries, AfXdpError>
admit_af_xdp_ring_entries(std::uint32_t entries) noexcept {
    return ::fixy::admit_refined<af_xdp_ring_depth>(entries, AfXdpError::InvalidRingSize);
}

// Two powers of two multiply to a power of two, so the product needs no
// check of its own.  What can fail is its width: a socket names its UMEM
// size as a 32-bit template argument, so a config whose UMEM does not fit
// in 32 bits could never match a socket.
[[nodiscard]] constexpr std::expected<DeclaredAfXdpConfig, AfXdpError>
mint_af_xdp_config(NicInterfaceName interface, AfXdpIfIndex ifindex, AfXdpQueueId queue_id, AfXdpFrameSize frame_size,
                   AfXdpFrameCount frame_count, AfXdpRingEntries fill_ring_size, AfXdpRingEntries completion_ring_size,
                   AfXdpRingEntries rx_ring_size, AfXdpRingEntries tx_ring_size, AfXdpMode mode = AfXdpMode::ZeroCopy,
                   bool require_xdp_redirect = true) noexcept {
    const std::uint64_t umem_bytes = static_cast<std::uint64_t>(frame_size.value()) * frame_count.value();
    if (umem_bytes > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(AfXdpError::InvalidUmemShape);
    }
    return ::fixy::mint_tagged<::fixy::tags::source::AfXdp>(AfXdpConfig{
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
    });
}

template <std::uint32_t UmemBytes, std::uint32_t FrameSize, std::uint32_t FillRing, std::uint32_t CompletionRing,
          std::uint32_t RxRing, std::uint32_t TxRing>
concept AfXdpStaticShape =
    ::fixy::power_of_two(UmemBytes) && af_xdp_frame_bytes(FrameSize) && (UmemBytes % FrameSize) == 0u
    && af_xdp_ring_depth(UmemBytes / FrameSize) && af_xdp_ring_depth(FillRing) && af_xdp_ring_depth(CompletionRing)
    && af_xdp_ring_depth(RxRing) && af_xdp_ring_depth(TxRing);

// Minting a socket allocates its UMEM, so the context must own Alloc as
// well as the startup capability.
template <class Ctx>
concept CtxFitsAfXdpMint =
    ::foundation::effects::CtxOwnsAllOf<Ctx, ::foundation::effects::Effect::Init, ::foundation::effects::Effect::Alloc>;

template <std::uint32_t UmemBytes, std::uint32_t FrameSize, std::uint32_t FillRing, std::uint32_t CompletionRing,
          std::uint32_t RxRing, std::uint32_t TxRing>
[[nodiscard]] constexpr bool af_xdp_config_matches_static_shape(DeclaredAfXdpConfig const& config) noexcept {
    AfXdpConfig const& raw = config.value();
    return raw.frame_size.value() == FrameSize && raw.frame_count.value() == (UmemBytes / FrameSize)
        && raw.fill_ring_size.value() == FillRing && raw.completion_ring_size.value() == CompletionRing
        && raw.rx_ring_size.value() == RxRing && raw.tx_ring_size.value() == TxRing;
}

template <std::uint32_t UmemBytes, std::uint32_t FrameSize, std::uint32_t FillRing = 2048,
          std::uint32_t CompletionRing = 2048, std::uint32_t RxRing = 2048, std::uint32_t TxRing = 2048>
    requires AfXdpStaticShape<UmemBytes, FrameSize, FillRing, CompletionRing, RxRing, TxRing>
class AfXdpSocket
    : public ::foundation::Pinned<AfXdpSocket<UmemBytes, FrameSize, FillRing, CompletionRing, RxRing, TxRing>> {
public:
    using byte_type = std::byte;
    using umem_type = ::foundation::AlignedBuffer<byte_type, 4096>;
    using packet_view = ::fixy::Borrowed<byte_type, AfXdpSocket>;

    // An RX frame carries bytes that originate off the wire, so it is handed
    // out External-tagged and cannot reach a sanitized-only consumer without
    // passing sanitize_rx_frame.  A TX frame is minted here rather than
    // received, so it stays an untagged packet_view.
    using rx_frame = ::fixy::Tagged<packet_view, ::fixy::tags::source::External>;
    using sanitized_frame = ::fixy::Tagged<packet_view, ::fixy::tags::source::Sanitized>;

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
        static_assert(::fixy::power_of_two(Capacity));

        std::array<Descriptor, Capacity> slots_{};
        std::uint32_t head_ = 0;
        std::uint32_t tail_ = 0;
        std::uint32_t size_ = 0;

    public:
        [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }

        [[nodiscard]] constexpr bool full() const noexcept { return size_ == Capacity; }

        [[nodiscard]] constexpr std::uint32_t size() const noexcept { return size_; }

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

    // The UMEM is private and the socket is pinned, so the buffer has one
    // owner for the socket's whole life and dies with it.
    DeclaredAfXdpConfig config_;
    umem_type umem_;
    Ring<FillRing> fill_{};
    Ring<CompletionRing> completion_{};
    Ring<RxRing> rx_{};
    Ring<TxRing> tx_{};
    std::uint32_t next_frame_ = 0;

    explicit AfXdpSocket(DeclaredAfXdpConfig config)
        : config_{std::move(config)}, umem_{umem_type::allocate(UmemBytes)} {}

    [[nodiscard]] CRUCIBLE_HOT packet_view view(Descriptor desc) noexcept {
        byte_type* base = umem_.data();
        return packet_view{base + static_cast<std::size_t>(desc.frame_id) * FrameSize, desc.length};
    }

public:
    template <class Ctx>
        requires CtxFitsAfXdpMint<Ctx>
    [[nodiscard]] static AfXdpSocket mint(Ctx const&, DeclaredAfXdpConfig config) {
        CRUCIBLE_PRE(
            (af_xdp_config_matches_static_shape<UmemBytes, FrameSize, FillRing, CompletionRing, RxRing, TxRing>(
                config)));
        return AfXdpSocket{std::move(config)};
    }

    [[nodiscard]] constexpr AfXdpConfig const& config() const noexcept { return config_.value(); }

    [[nodiscard]] constexpr std::uint32_t tx_pending() const noexcept { return tx_.size(); }

    [[nodiscard]] constexpr std::uint32_t rx_pending() const noexcept { return rx_.size(); }

    [[nodiscard]] constexpr std::uint32_t completions_pending() const noexcept { return completion_.size(); }

    [[nodiscard]] CRUCIBLE_HOT std::optional<packet_view> alloc_tx_buffer(std::uint32_t length) noexcept {
        if (length == 0 || length > FrameSize || next_frame_ >= frame_count) {
            return std::nullopt;
        }
        const Descriptor desc{.frame_id = next_frame_, .length = length};
        ++next_frame_;
        return view(desc);
    }

    [[nodiscard]] CRUCIBLE_HOT std::expected<void, AfXdpError> enqueue_tx(packet_view packet) noexcept {
        if (packet.empty() || packet.size() > FrameSize) {
            return std::unexpected(AfXdpError::PacketTooLarge);
        }
        byte_type* base = umem_.data();
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

    [[nodiscard]] CRUCIBLE_HOT std::optional<rx_frame> dequeue_rx() noexcept {
        auto desc = rx_.pop();
        if (!desc.has_value()) {
            return std::nullopt;
        }
        return ::fixy::mint_tagged<::fixy::tags::source::External>(view(*desc));
    }

    // The one place an RX frame goes from External to Sanitized.  A frame
    // that fails the bounds check is dropped rather than retagged, so it
    // never reaches a sanitized-only consumer.
    [[nodiscard]] CRUCIBLE_HOT static std::optional<sanitized_frame> sanitize_rx_frame(rx_frame&& frame) noexcept {
        packet_view const& view_ref = frame.value();
        if (view_ref.empty() || view_ref.size() > FrameSize) {
            return std::nullopt;
        }
        return std::move(frame).template retag<::fixy::tags::source::Sanitized>();
    }

    [[nodiscard]] CRUCIBLE_HOT std::uint32_t poll() noexcept { return rx_.size() + completion_.size(); }

    [[nodiscard]] CRUCIBLE_HOT bool stage_rx_descriptor(std::uint32_t frame_id, std::uint32_t length) noexcept {
        if (frame_id >= frame_count || length == 0 || length > FrameSize) {
            return false;
        }
        return rx_.push(Descriptor{.frame_id = frame_id, .length = length});
    }
};

template <std::uint32_t UmemBytes, std::uint32_t FrameSize, std::uint32_t FillRing = 2048,
          std::uint32_t CompletionRing = 2048, std::uint32_t RxRing = 2048, std::uint32_t TxRing = 2048, class Ctx>
    requires AfXdpStaticShape<UmemBytes, FrameSize, FillRing, CompletionRing, RxRing, TxRing> && CtxFitsAfXdpMint<Ctx>
// The one runtime step below this factory is the UMEM allocation, and that
// step calls std::abort() when the allocator returns no memory.  No step in
// the chain throws, so the factory declares that instead of resting on the
// whole-artifact check that utils/scripts/check-no-throw-no-rtti.sh does.
[[nodiscard]] AfXdpSocket<UmemBytes, FrameSize, FillRing, CompletionRing, RxRing, TxRing>
mint_af_xdp_socket(Ctx const& ctx, DeclaredAfXdpConfig config) noexcept {
    return AfXdpSocket<UmemBytes, FrameSize, FillRing, CompletionRing, RxRing, TxRing>::mint(ctx, std::move(config));
}

}  // namespace crucible::cntp
