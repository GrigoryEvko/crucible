#pragma once

#include <crucible/NumericalRecipe.h>
#include <crucible/TensorMeta.h>
#include <crucible/Types.h>
#include <crucible/cog/CogIdentity.h>
#include <crucible/forge/recipes/Network.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Row.h>
#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>

#include <array>
#include <cstddef>
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

// The order of these is load-bearing. A kind belongs to the category
// whose range of values it falls in, so one declared outside its own
// group silently joins another.
enum class Ir001OpKind : std::uint16_t {
    Gemm = 0,
    Conv,
    Attention,
    Reduction,
    Elementwise,
    Softmax,
    LayerNorm,
    Scan,
    CopyHostToDevice,
    CopyDeviceToHost,
    CopyDeviceToDevice,
    NvlinkP2pCopy,
    PciePeerCopy,
    TmaGatherScatter,
    Prefetch,
    Evict,
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
    AsyncSendBatch,
    AsyncRecvBatch,
    GossipRound,
    EventualAggregate,
    BarrierWithQuorum,
    LeaseAcquire,
    LeaseRelease,
    AtomicAddRemote,
    SemaphoreWait,
    SemaphorePost,
    LoadNvme,
    StoreNvme,
    Checkpoint,
    Restore,
    Branch,
    Loop,
    Fork,
    Join,
    CounterRead,
    SamplePmu,
    SampleThermal,
    WirePcapEmit,
    IntTelemetryEmit,
    LldpSend,
    LldpRecv,
    SwimPing,
    SwimAck,
    SwimIndirectPing,
    ScuttlebuttDeltaSend,
};

inline constexpr auto kIr001OpKindCount = static_cast<std::uint16_t>(::foundation::reflect::enum_count<Ir001OpKind>);
inline constexpr std::uint16_t kIr001MaxParticipants = 4096;
inline constexpr std::uint32_t kIr001MaxTimeoutMs = 600000;

inline constexpr auto kIr001ParticipantRange = ::fixy::in_range<std::uint16_t{1}, kIr001MaxParticipants>;
inline constexpr auto kIr001TimeoutRange = ::fixy::in_range<std::uint32_t{0}, kIr001MaxTimeoutMs>;

using Ir001ParticipantCount = ::fixy::Refined<kIr001ParticipantRange, std::uint16_t>;
using Ir001QuorumCount = ::fixy::Refined<kIr001ParticipantRange, std::uint16_t>;
using Ir001TimeoutMs = ::fixy::Refined<kIr001TimeoutRange, std::uint32_t>;

struct Ir001OpInfo {
    Ir001OpKind kind = Ir001OpKind::Gemm;
    Ir001OpCategory category = Ir001OpCategory::Compute;
    std::string_view name{};
    bool side_effecting = false;
    bool network_visible = false;
};

[[nodiscard]] constexpr Ir001OpCategory ir001_op_category(Ir001OpKind kind) noexcept {
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

[[nodiscard]] constexpr bool ir001_kind_network_visible(Ir001OpKind kind) noexcept {
    auto const category = ir001_op_category(kind);
    return category == Ir001OpCategory::PointToPoint || category == Ir001OpCategory::CollectiveSync
        || category == Ir001OpCategory::CollectiveAsync || category == Ir001OpCategory::Coordination
        || category == Ir001OpCategory::Telemetry || category == Ir001OpCategory::Discovery;
}

[[nodiscard]] constexpr bool ir001_kind_side_effecting(Ir001OpKind kind) noexcept {
    auto const category = ir001_op_category(kind);
    return category != Ir001OpCategory::Compute && kind != Ir001OpKind::Prefetch;
}

// The display name of an op kind is its enumerator identifier in lower
// snake case, so CopyHostToDevice reads copy_host_to_device.  Deriving the
// name from the enumerator keeps a new kind from shipping without one.  A
// value that no enumerator holds names itself "<unknown Ir001OpKind>".
[[nodiscard]] constexpr std::string_view ir001_op_kind_name(Ir001OpKind kind) noexcept {
    return ::foundation::reflect::enum_words<Ir001OpKind, '_'>(kind);
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
concept Ir001CollectiveKind = ir001_op_category(Kind) == Ir001OpCategory::CollectiveSync
                           || ir001_op_category(Kind) == Ir001OpCategory::CollectiveAsync;

template <Ir001OpKind Kind>
concept Ir001PointToPointKind = ir001_op_category(Kind) == Ir001OpCategory::PointToPoint;

struct TensorPort {
    TensorMeta meta{};
    SlotId slot = SlotId::none();
};

using DeclaredPeerSet = ::fixy::Tagged<std::span<const cog::CogIdentity>, ::fixy::tags::source::Ir001>;

struct PeerSetRef {
    DeclaredPeerSet peers = ::fixy::mint_tagged<::fixy::tags::source::Ir001>(std::span<const cog::CogIdentity>{});
    Ir001ParticipantCount count = ::fixy::mint_refined<kIr001ParticipantRange>(std::uint16_t{1});
};

struct Ir001WireHeader {
    std::uint16_t kind = 0;
    std::uint16_t flags = 0;
    std::uint32_t attr_words = 0;
    std::uint64_t content_hash = 0;
};

namespace detail {
// The content key folds through the one combine_ids body of the tree, so a
// change to the salt, the mix or the finalizer reaches this key as well.
using ::foundation::reflect::combine_ids;
using ::foundation::reflect::fmix64;

template <typename E>
[[nodiscard]] constexpr std::uint64_t enum_hash_word(E value) noexcept {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(std::to_underlying(value)));
}

[[nodiscard]] constexpr std::uint64_t hash_tensor_meta(std::uint64_t h, TensorMeta const& meta) noexcept {
    h = combine_ids(h, meta.ndim);
    h = combine_ids(h, enum_hash_word(meta.dtype));
    h = combine_ids(h, enum_hash_word(meta.device_type));
    h = combine_ids(h, static_cast<std::uint64_t>(static_cast<std::int64_t>(meta.device_idx)));
    h = combine_ids(h, enum_hash_word(meta.layout));
    h = combine_ids(h, meta.requires_grad ? 1U : 0U);
    h = combine_ids(h, meta.flags);
    h = combine_ids(h, meta.output_nr);
    h = combine_ids(h, static_cast<std::uint64_t>(meta.storage_offset));
    h = combine_ids(h, meta.version);
    h = combine_ids(h, meta.storage_nbytes);
    for (std::uint8_t i = 0; i < kMaxTensorNDim; ++i) {
        h = combine_ids(h, static_cast<std::uint64_t>(meta.sizes[i].value()));
        h = combine_ids(h, static_cast<std::uint64_t>(meta.strides[i].value()));
    }
    return h;
}

[[nodiscard]] constexpr std::uint64_t hash_tensor_port(std::uint64_t h, TensorPort const& port) noexcept {
    h = hash_tensor_meta(h, port.meta);
    return combine_ids(h, port.slot.raw());
}

[[nodiscard]] constexpr std::uint64_t hash_cog_identity(std::uint64_t h, cog::CogIdentity const& peer) noexcept {
    h = combine_ids(h, peer.uuid.hi);
    h = combine_ids(h, peer.uuid.lo);
    h = combine_ids(h, enum_hash_word(peer.level));
    return combine_ids(h, enum_hash_word(peer.kind));
}

[[nodiscard]] constexpr std::uint64_t hash_peer_set(std::uint64_t h, PeerSetRef const& participants) noexcept {
    auto const peers = participants.peers.value();
    h = combine_ids(h, participants.count.value());
    h = combine_ids(h, peers.size());
    auto const declared = static_cast<std::size_t>(participants.count.value());
    auto const n = peers.size() < declared ? peers.size() : declared;
    for (std::size_t i = 0; i < n; ++i) {
        h = hash_cog_identity(h, peers[i]);
    }
    return h;
}

[[nodiscard]] constexpr std::uint64_t hash_recipe_semantics(std::uint64_t h, NumericalRecipe const& recipe) noexcept {
    return combine_ids(h, compute_recipe_hash(recipe).raw());
}
}  // namespace detail

template <Ir001OpKind Kind, class Attrs, class Row = ::foundation::effects::Row<>>
struct Ir001Node {
    using attrs_type = Attrs;
    using row_type = Row;
    static constexpr Ir001OpKind kind = Kind;

    Attrs attrs{};
    ContentHash content_hash{};
};

template <class T>
concept Ir001NodeLike = requires(T node) {
    typename T::attrs_type;
    typename T::row_type;
    { T::kind } -> std::convertible_to<Ir001OpKind>;
    { node.attrs } -> std::same_as<typename T::attrs_type&>;
    { node.content_hash } -> std::same_as<ContentHash&>;
};

template <Ir001NodeLike Node>
using DeclaredIr001Node = ::fixy::Tagged<Node, ::fixy::tags::source::Ir001>;

struct CollectiveAttrs {
    TensorPort input{};
    TensorPort output{};
    PeerSetRef participants{};
    NumericalRecipe recipe{};
    recipes::NetworkCollectiveAlgorithm algorithm = recipes::NetworkCollectiveAlgorithm::Ring;
};

struct PointToPointAttrs {
    TensorPort payload{};
    cog::CogIdentity peer{};
    Ir001TimeoutMs timeout_ms = ::fixy::mint_refined<kIr001TimeoutRange>(std::uint32_t{0});
};

struct BarrierAttrs {
    PeerSetRef participants{};
    Ir001QuorumCount quorum = ::fixy::mint_refined<kIr001ParticipantRange>(std::uint16_t{1});
    Ir001TimeoutMs timeout_ms = ::fixy::mint_refined<kIr001TimeoutRange>(std::uint32_t{0});
};

struct StorageAttrs {
    TensorPort tensor{};
    ContentHash object{};
};

struct TelemetryAttrs {
    RowHash row{};
    std::uint64_t value = 0;
};

template <Ir001OpKind Kind, class Row = ::foundation::effects::Row<>>
    requires Ir001CollectiveKind<Kind>
using CollectiveOp = Ir001Node<Kind, CollectiveAttrs, Row>;

template <Ir001OpKind Kind, class Row = ::foundation::effects::Row<>>
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
using TelemetryEmitOp = Ir001Node<Ir001OpKind::IntTelemetryEmit, TelemetryAttrs>;

template <Ir001NodeLike Node>
[[nodiscard]] constexpr ContentHash compute_ir001_content_hash(Node const& node) noexcept {
    auto h = detail::fmix64(0x4952303031ULL);
    h = detail::combine_ids(h, std::to_underlying(Node::kind));
    h = detail::combine_ids(h, sizeof(typename Node::attrs_type));
    if constexpr (std::same_as<typename Node::attrs_type, CollectiveAttrs>) {
        h = detail::hash_tensor_port(h, node.attrs.input);
        h = detail::hash_tensor_port(h, node.attrs.output);
        h = detail::hash_peer_set(h, node.attrs.participants);
        h = detail::hash_recipe_semantics(h, node.attrs.recipe);
        h = detail::combine_ids(h, std::to_underlying(node.attrs.algorithm));
    } else if constexpr (std::same_as<typename Node::attrs_type, PointToPointAttrs>) {
        h = detail::hash_tensor_port(h, node.attrs.payload);
        h = detail::hash_cog_identity(h, node.attrs.peer);
        h = detail::combine_ids(h, node.attrs.timeout_ms.value());
    } else if constexpr (std::same_as<typename Node::attrs_type, BarrierAttrs>) {
        h = detail::hash_peer_set(h, node.attrs.participants);
        h = detail::combine_ids(h, node.attrs.quorum.value());
        h = detail::combine_ids(h, node.attrs.timeout_ms.value());
    } else if constexpr (std::same_as<typename Node::attrs_type, StorageAttrs>) {
        h = detail::hash_tensor_port(h, node.attrs.tensor);
        h = detail::combine_ids(h, node.attrs.object.raw());
    } else if constexpr (std::same_as<typename Node::attrs_type, TelemetryAttrs>) {
        h = detail::combine_ids(h, node.attrs.row.raw());
        h = detail::combine_ids(h, node.attrs.value);
    }
    return ContentHash::from_raw(h == 0 ? 1 : h);
}

template <Ir001NodeLike Node>
[[nodiscard]] constexpr DeclaredIr001Node<Node> admit_ir001_node(Node node) noexcept {
    node.content_hash = compute_ir001_content_hash(node);
    return ::fixy::mint_tagged<::fixy::tags::source::Ir001>(node);
}

template <Ir001NodeLike Node, class Visitor>
constexpr decltype(auto) visit_ir001_node(DeclaredIr001Node<Node> node, Visitor&& visitor) {
    return std::forward<Visitor>(visitor)(node.value());
}

template <Ir001NodeLike Node>
[[nodiscard]] constexpr Ir001WireHeader serialize_ir001_header(DeclaredIr001Node<Node> node) noexcept {
    return Ir001WireHeader{
        .kind = std::to_underlying(Node::kind),
        .flags = ir001_kind_side_effecting(Node::kind) ? std::uint16_t{1} : std::uint16_t{0},
        .attr_words = static_cast<std::uint32_t>((sizeof(typename Node::attrs_type) + 7U) / 8U),
        .content_hash = node.value().content_hash.raw(),
    };
}

// The category ladder reads the kinds as one dense range from zero, so the
// last enumerator must sit at the count minus one.
static_assert(kIr001OpKindCount == std::to_underlying(Ir001OpKind::ScuttlebuttDeltaSend) + 1U);
static_assert(sizeof(Ir001ParticipantCount) == sizeof(std::uint16_t));
static_assert(sizeof(Ir001WireHeader) == 16);
static_assert(std::is_trivially_copyable_v<Ir001WireHeader>);
static_assert(Ir001CollectiveKind<Ir001OpKind::AllReduce>);
static_assert(!Ir001CollectiveKind<Ir001OpKind::SendAsync>);
static_assert(Ir001PointToPointKind<Ir001OpKind::RecvAsync>);
static_assert(!Ir001PointToPointKind<Ir001OpKind::AllGather>);

}  // namespace crucible::forge::ir001
