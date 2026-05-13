#pragma once

// GAPS-167. Forge IR001 comm-through-IR substrate.
//
// This is the op taxonomy and typed-node carrier only. Forge phases,
// IR002 KernelNode lowering, Mimic network backends, and cross-vendor
// CI consume this surface in later tasks; they are not implemented here.

#include <crucible/NumericalRecipe.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/forge/recipes/Network.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/RefinedAlgebra.h>
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstdint>
#include <concepts>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::forge::ir001 {

enum class Ir001OpCategory : std::uint8_t {
    Compute = 0,
    Memory,
    PointToPoint,
    CollectiveSync,
    CollectiveAsync,
    Coordination,
    Storage,
    Control,
    Telemetry,
    Discovery,
};

enum class Ir001OpKind : std::uint16_t {
    // Compute.
    Gemm = 0,
    Conv,
    Attention,
    Reduction,
    Elementwise,
    Softmax,
    LayerNorm,
    Scan,
    // Memory movement.
    CopyHostToDevice,
    CopyDeviceToHost,
    CopyDeviceToDevice,
    NvlinkP2pCopy,
    PciePeerCopy,
    TmaGatherScatter,
    Prefetch,
    Evict,
    // Point-to-point communication.
    SendSync,
    SendAsync,
    SendWithCompletion,
    SendInline,
    RecvSync,
    RecvAsync,
    RecvWithCompletion,
    RecvIntoExistingBuffer,
    SendRecv,
    Put,
    Get,
    AtomicCompareExchange,
    AtomicFetchAdd,
    // Synchronous collectives.
    AllReduce,
    AllGather,
    AllGatherV,
    ReduceScatter,
    Broadcast,
    MultiRootBroadcast,
    Scatter,
    Gather,
    GatherV,
    AllToAll,
    AllToAllV,
    Barrier,
    BarrierWithTimeout,
    SparseAllToAll,
    // Asynchronous collectives.
    AsyncSendBatch,
    AsyncRecvBatch,
    GossipRound,
    EventualAggregate,
    // Coordination.
    BarrierWithQuorum,
    LeaseAcquire,
    LeaseRelease,
    AtomicAddRemote,
    SemaphoreWait,
    SemaphorePost,
    // Storage.
    LoadNvme,
    StoreNvme,
    Checkpoint,
    Restore,
    // Control.
    Branch,
    Loop,
    Fork,
    Join,
    // Telemetry.
    CounterRead,
    SamplePmu,
    SampleThermal,
    WirePcapEmit,
    IntTelemetryEmit,
    // Discovery.
    LldpSend,
    LldpRecv,
    SwimPing,
    SwimAck,
    SwimIndirectPing,
    ScuttlebuttDeltaSend,
};

inline constexpr std::uint16_t kIr001OpKindCount = 72;
inline constexpr std::uint8_t kIr001MaxPorts = 16;
inline constexpr std::uint16_t kIr001MaxParticipants = 4096;

using Ir001PortCount =
    safety::Bounded<std::uint8_t{0}, kIr001MaxPorts, std::uint8_t>;
using Ir001ParticipantCount =
    safety::Bounded<std::uint16_t{1}, kIr001MaxParticipants, std::uint16_t>;
using Ir001QuorumCount =
    safety::Bounded<std::uint16_t{1}, kIr001MaxParticipants, std::uint16_t>;
using Ir001TimeoutMs =
    safety::Bounded<std::uint32_t{0}, std::uint32_t{600'000}, std::uint32_t>;

struct Ir001OpInfo {
    Ir001OpKind kind = Ir001OpKind::Gemm;
    Ir001OpCategory category = Ir001OpCategory::Compute;
    std::string_view name{};
    bool side_effecting = false;
    bool network_visible = false;
};

[[nodiscard]] constexpr Ir001OpCategory
ir001_op_category(Ir001OpKind kind) noexcept {
    auto const k = std::to_underlying(kind);
    if (k <= std::to_underlying(Ir001OpKind::Scan)) {
        return Ir001OpCategory::Compute;
    }
    if (k <= std::to_underlying(Ir001OpKind::Evict)) {
        return Ir001OpCategory::Memory;
    }
    if (k <= std::to_underlying(Ir001OpKind::AtomicFetchAdd)) {
        return Ir001OpCategory::PointToPoint;
    }
    if (k <= std::to_underlying(Ir001OpKind::SparseAllToAll)) {
        return Ir001OpCategory::CollectiveSync;
    }
    if (k <= std::to_underlying(Ir001OpKind::EventualAggregate)) {
        return Ir001OpCategory::CollectiveAsync;
    }
    if (k <= std::to_underlying(Ir001OpKind::SemaphorePost)) {
        return Ir001OpCategory::Coordination;
    }
    if (k <= std::to_underlying(Ir001OpKind::Restore)) {
        return Ir001OpCategory::Storage;
    }
    if (k <= std::to_underlying(Ir001OpKind::Join)) {
        return Ir001OpCategory::Control;
    }
    if (k <= std::to_underlying(Ir001OpKind::IntTelemetryEmit)) {
        return Ir001OpCategory::Telemetry;
    }
    return Ir001OpCategory::Discovery;
}

[[nodiscard]] constexpr bool ir001_kind_network_visible(
    Ir001OpKind kind) noexcept {
    auto const category = ir001_op_category(kind);
    return category == Ir001OpCategory::PointToPoint
        || category == Ir001OpCategory::CollectiveSync
        || category == Ir001OpCategory::CollectiveAsync
        || category == Ir001OpCategory::Coordination
        || category == Ir001OpCategory::Telemetry
        || category == Ir001OpCategory::Discovery;
}

[[nodiscard]] constexpr bool ir001_kind_side_effecting(
    Ir001OpKind kind) noexcept {
    auto const category = ir001_op_category(kind);
    return category != Ir001OpCategory::Compute
        && kind != Ir001OpKind::Prefetch;
}

[[nodiscard]] constexpr std::string_view
ir001_op_kind_name(Ir001OpKind kind) noexcept {
    using enum Ir001OpKind;
    switch (kind) {
        case Gemm: return "gemm";
        case Conv: return "conv";
        case Attention: return "attention";
        case Reduction: return "reduction";
        case Elementwise: return "elementwise";
        case Softmax: return "softmax";
        case LayerNorm: return "layernorm";
        case Scan: return "scan";
        case CopyHostToDevice: return "copy_host_to_device";
        case CopyDeviceToHost: return "copy_device_to_host";
        case CopyDeviceToDevice: return "copy_device_to_device";
        case NvlinkP2pCopy: return "nvlink_p2p_copy";
        case PciePeerCopy: return "pcie_peer_copy";
        case TmaGatherScatter: return "tma_gather_scatter";
        case Prefetch: return "prefetch";
        case Evict: return "evict";
        case SendSync: return "send_sync";
        case SendAsync: return "send_async";
        case SendWithCompletion: return "send_with_completion";
        case SendInline: return "send_inline";
        case RecvSync: return "recv_sync";
        case RecvAsync: return "recv_async";
        case RecvWithCompletion: return "recv_with_completion";
        case RecvIntoExistingBuffer: return "recv_into_existing_buffer";
        case SendRecv: return "sendrecv";
        case Put: return "put";
        case Get: return "get";
        case AtomicCompareExchange: return "atomic_compare_exchange";
        case AtomicFetchAdd: return "atomic_fetch_add";
        case AllReduce: return "all_reduce";
        case AllGather: return "all_gather";
        case AllGatherV: return "all_gather_v";
        case ReduceScatter: return "reduce_scatter";
        case Broadcast: return "broadcast";
        case MultiRootBroadcast: return "multi_root_broadcast";
        case Scatter: return "scatter";
        case Gather: return "gather";
        case GatherV: return "gather_v";
        case AllToAll: return "all_to_all";
        case AllToAllV: return "all_to_all_v";
        case Barrier: return "barrier";
        case BarrierWithTimeout: return "barrier_with_timeout";
        case SparseAllToAll: return "sparse_all_to_all";
        case AsyncSendBatch: return "async_send_batch";
        case AsyncRecvBatch: return "async_recv_batch";
        case GossipRound: return "gossip_round";
        case EventualAggregate: return "eventual_aggregate";
        case BarrierWithQuorum: return "barrier_with_quorum";
        case LeaseAcquire: return "lease_acquire";
        case LeaseRelease: return "lease_release";
        case AtomicAddRemote: return "atomic_add_remote";
        case SemaphoreWait: return "semaphore_wait";
        case SemaphorePost: return "semaphore_post";
        case LoadNvme: return "load_nvme";
        case StoreNvme: return "store_nvme";
        case Checkpoint: return "checkpoint";
        case Restore: return "restore";
        case Branch: return "branch";
        case Loop: return "loop";
        case Fork: return "fork";
        case Join: return "join";
        case CounterRead: return "counter_read";
        case SamplePmu: return "sample_pmu";
        case SampleThermal: return "sample_thermal";
        case WirePcapEmit: return "wire_pcap_emit";
        case IntTelemetryEmit: return "int_telemetry_emit";
        case LldpSend: return "lldp_send";
        case LldpRecv: return "lldp_recv";
        case SwimPing: return "swim_ping";
        case SwimAck: return "swim_ack";
        case SwimIndirectPing: return "swim_indirect_ping";
        case ScuttlebuttDeltaSend: return "scuttlebutt_delta_send";
        default: return "<unknown Ir001OpKind>";
    }
}

[[nodiscard]] constexpr Ir001OpInfo ir001_op_info(Ir001OpKind kind) noexcept {
    return Ir001OpInfo{
        .kind = kind,
        .category = ir001_op_category(kind),
        .name = ir001_op_kind_name(kind),
        .side_effecting = ir001_kind_side_effecting(kind),
        .network_visible = ir001_kind_network_visible(kind),
    };
}

template <Ir001OpKind Kind>
concept Ir001CollectiveKind =
    ir001_op_category(Kind) == Ir001OpCategory::CollectiveSync
    || ir001_op_category(Kind) == Ir001OpCategory::CollectiveAsync;

template <Ir001OpKind Kind>
concept Ir001PointToPointKind =
    ir001_op_category(Kind) == Ir001OpCategory::PointToPoint;

struct TensorPort {
    TensorMeta meta{};
    SlotId slot = SlotId::none();
};

using DeclaredPeerSet =
    safety::Tagged<std::span<const cog::CogIdentity>, safety::source::Ir001>;

struct PeerSetRef {
    DeclaredPeerSet peers{std::span<const cog::CogIdentity>{}};
    Ir001ParticipantCount count{std::uint16_t{1},
                                typename Ir001ParticipantCount::Trusted{}};
};

struct Ir001WireHeader {
    std::uint16_t kind = 0;
    std::uint16_t flags = 0;
    std::uint32_t attr_words = 0;
    std::uint64_t content_hash = 0;
};

namespace detail {
[[nodiscard]] constexpr std::uint64_t fmix64(std::uint64_t k) noexcept {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return k;
}

[[nodiscard]] constexpr std::uint64_t hash_mix(std::uint64_t h,
                                               std::uint64_t v) noexcept {
    return fmix64(h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2)));
}

template <typename E>
[[nodiscard]] constexpr std::uint64_t enum_hash_word(E value) noexcept {
    return static_cast<std::uint64_t>(
        static_cast<std::int64_t>(std::to_underlying(value)));
}

[[nodiscard]] constexpr std::uint64_t hash_tensor_meta(
    std::uint64_t h, TensorMeta const& meta) noexcept {
    h = hash_mix(h, meta.ndim);
    h = hash_mix(h, enum_hash_word(meta.dtype));
    h = hash_mix(h, enum_hash_word(meta.device_type));
    h = hash_mix(h, static_cast<std::uint64_t>(
        static_cast<std::int64_t>(meta.device_idx)));
    h = hash_mix(h, enum_hash_word(meta.layout));
    h = hash_mix(h, meta.requires_grad ? 1U : 0U);
    h = hash_mix(h, meta.flags);
    h = hash_mix(h, meta.output_nr);
    h = hash_mix(h, static_cast<std::uint64_t>(meta.storage_offset));
    h = hash_mix(h, meta.version);
    h = hash_mix(h, meta.storage_nbytes);
    for (std::uint8_t i = 0; i < kMaxTensorNDim; ++i) {
        h = hash_mix(h, static_cast<std::uint64_t>(meta.sizes[i].value()));
        h = hash_mix(h, static_cast<std::uint64_t>(meta.strides[i].value()));
    }
    return h;
}

[[nodiscard]] constexpr std::uint64_t hash_tensor_port(
    std::uint64_t h, TensorPort const& port) noexcept {
    h = hash_tensor_meta(h, port.meta);
    return hash_mix(h, port.slot.raw());
}

[[nodiscard]] constexpr std::uint64_t hash_cog_identity(
    std::uint64_t h, cog::CogIdentity const& peer) noexcept {
    h = hash_mix(h, peer.uuid.hi);
    h = hash_mix(h, peer.uuid.lo);
    h = hash_mix(h, enum_hash_word(peer.level));
    return hash_mix(h, enum_hash_word(peer.kind));
}

[[nodiscard]] constexpr std::uint64_t hash_peer_set(
    std::uint64_t h, PeerSetRef const& participants) noexcept {
    auto const peers = participants.peers.value();
    h = hash_mix(h, participants.count.value());
    h = hash_mix(h, peers.size());
    auto const declared = static_cast<std::size_t>(participants.count.value());
    auto const n = peers.size() < declared ? peers.size() : declared;
    for (std::size_t i = 0; i < n; ++i) {
        h = hash_cog_identity(h, peers[i]);
    }
    return h;
}

[[nodiscard]] constexpr std::uint64_t hash_recipe_semantics(
    std::uint64_t h, NumericalRecipe const& recipe) noexcept {
    return hash_mix(h, compute_recipe_hash(recipe).raw());
}
}  // namespace detail

template <Ir001OpKind Kind, class Attrs, class Row = effects::Row<>>
struct Ir001Node {
    using attrs_type = Attrs;
    using row_type = Row;
    static constexpr Ir001OpKind kind = Kind;

    Attrs attrs{};
    ContentHash content_hash{};
};

template <class T>
concept Ir001NodeLike =
    requires(T node) {
        typename T::attrs_type;
        typename T::row_type;
        { T::kind } -> std::convertible_to<Ir001OpKind>;
        { node.attrs } -> std::same_as<typename T::attrs_type&>;
        { node.content_hash } -> std::same_as<ContentHash&>;
    };

template <Ir001NodeLike Node>
using DeclaredIr001Node = safety::Tagged<Node, safety::source::Ir001>;

struct CollectiveAttrs {
    TensorPort input{};
    TensorPort output{};
    PeerSetRef participants{};
    NumericalRecipe recipe{};
    recipes::NetworkCollectiveAlgorithm algorithm =
        recipes::NetworkCollectiveAlgorithm::Ring;
};

struct PointToPointAttrs {
    TensorPort payload{};
    cog::CogIdentity peer{};
    Ir001TimeoutMs timeout_ms{std::uint32_t{0},
                              typename Ir001TimeoutMs::Trusted{}};
};

struct BarrierAttrs {
    PeerSetRef participants{};
    Ir001QuorumCount quorum{std::uint16_t{1},
                            typename Ir001QuorumCount::Trusted{}};
    Ir001TimeoutMs timeout_ms{std::uint32_t{0},
                              typename Ir001TimeoutMs::Trusted{}};
};

struct StorageAttrs {
    TensorPort tensor{};
    ContentHash object{};
};

struct TelemetryAttrs {
    RowHash row{};
    std::uint64_t value = 0;
};

template <Ir001OpKind Kind, class Row = effects::Row<>>
    requires Ir001CollectiveKind<Kind>
using CollectiveOp = Ir001Node<Kind, CollectiveAttrs, Row>;

template <Ir001OpKind Kind, class Row = effects::Row<>>
    requires Ir001PointToPointKind<Kind>
using PointToPointOp = Ir001Node<Kind, PointToPointAttrs, Row>;

using AllReduceOp = CollectiveOp<Ir001OpKind::AllReduce>;
using AllGatherOp = CollectiveOp<Ir001OpKind::AllGather>;
using ReduceScatterOp = CollectiveOp<Ir001OpKind::ReduceScatter>;
using BroadcastOp = CollectiveOp<Ir001OpKind::Broadcast>;
using SendOp = PointToPointOp<Ir001OpKind::SendAsync>;
using RecvOp = PointToPointOp<Ir001OpKind::RecvAsync>;
using BarrierOp = Ir001Node<Ir001OpKind::BarrierWithQuorum, BarrierAttrs>;
using CheckpointOp = Ir001Node<Ir001OpKind::Checkpoint, StorageAttrs>;
using TelemetryEmitOp =
    Ir001Node<Ir001OpKind::IntTelemetryEmit, TelemetryAttrs>;

template <Ir001NodeLike Node>
[[nodiscard]] constexpr ContentHash compute_ir001_content_hash(
    Node const& node) noexcept {
    auto h = detail::fmix64(0x4952303031ULL);
    h = detail::hash_mix(h, std::to_underlying(Node::kind));
    h = detail::hash_mix(h, sizeof(typename Node::attrs_type));
    if constexpr (std::same_as<typename Node::attrs_type, CollectiveAttrs>) {
        h = detail::hash_tensor_port(h, node.attrs.input);
        h = detail::hash_tensor_port(h, node.attrs.output);
        h = detail::hash_peer_set(h, node.attrs.participants);
        h = detail::hash_recipe_semantics(h, node.attrs.recipe);
        h = detail::hash_mix(h, std::to_underlying(node.attrs.algorithm));
    } else if constexpr (std::same_as<typename Node::attrs_type,
                                      PointToPointAttrs>) {
        h = detail::hash_tensor_port(h, node.attrs.payload);
        h = detail::hash_cog_identity(h, node.attrs.peer);
        h = detail::hash_mix(h, node.attrs.timeout_ms.value());
    } else if constexpr (std::same_as<typename Node::attrs_type,
                                      BarrierAttrs>) {
        h = detail::hash_peer_set(h, node.attrs.participants);
        h = detail::hash_mix(h, node.attrs.quorum.value());
        h = detail::hash_mix(h, node.attrs.timeout_ms.value());
    } else if constexpr (std::same_as<typename Node::attrs_type,
                                      StorageAttrs>) {
        h = detail::hash_tensor_port(h, node.attrs.tensor);
        h = detail::hash_mix(h, node.attrs.object.raw());
    } else if constexpr (std::same_as<typename Node::attrs_type,
                                      TelemetryAttrs>) {
        h = detail::hash_mix(h, node.attrs.row.raw());
        h = detail::hash_mix(h, node.attrs.value);
    }
    return ContentHash::from_raw(h == 0 ? 1 : h);
}

template <Ir001NodeLike Node>
[[nodiscard]] constexpr DeclaredIr001Node<Node>
admit_ir001_node(Node node) noexcept {
    node.content_hash = compute_ir001_content_hash(node);
    return DeclaredIr001Node<Node>{node};
}

template <Ir001NodeLike Node, class Visitor>
constexpr decltype(auto) visit_ir001_node(DeclaredIr001Node<Node> node,
                                          Visitor&& visitor) {
    return std::forward<Visitor>(visitor)(node.value());
}

template <Ir001NodeLike Node>
[[nodiscard]] constexpr Ir001WireHeader
serialize_ir001_header(DeclaredIr001Node<Node> node) noexcept {
    return Ir001WireHeader{
        .kind = std::to_underlying(Node::kind),
        .flags = ir001_kind_side_effecting(Node::kind) ? std::uint16_t{1}
                                                       : std::uint16_t{0},
        .attr_words = static_cast<std::uint32_t>(
            (sizeof(typename Node::attrs_type) + 7U) / 8U),
        .content_hash = node.value().content_hash.raw(),
    };
}

static_assert(kIr001OpKindCount
              == std::to_underlying(Ir001OpKind::ScuttlebuttDeltaSend) + 1U);
static_assert(sizeof(Ir001PortCount) == sizeof(std::uint8_t));
static_assert(sizeof(Ir001ParticipantCount) == sizeof(std::uint16_t));
static_assert(sizeof(Ir001WireHeader) == 16);
static_assert(std::is_trivially_copyable_v<Ir001WireHeader>);
static_assert(Ir001CollectiveKind<Ir001OpKind::AllReduce>);
static_assert(!Ir001CollectiveKind<Ir001OpKind::SendAsync>);
static_assert(Ir001PointToPointKind<Ir001OpKind::RecvAsync>);
static_assert(!Ir001PointToPointKind<Ir001OpKind::AllGather>);

}  // namespace crucible::forge::ir001
