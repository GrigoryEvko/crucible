#pragma once

// GAPS-169 WIP. Vendor-neutral sketch for Mimic network backends.
//
// This is parked under mimic/_wip because the real per-vendor network backend
// shape must be designed around the actual IR002 comm boundary and existing
// CNTP/MRC substrate, not this placeholder envelope. It does not emit RDMA
// work requests, AF_XDP descriptors, eBPF bytecode, switch programs, vendor SDK
// calls, or KernelCache entries. Until forge/Ir002/CommKernel.h exists, the
// request is anchored to an admitted IR001 comm node plus recipe constraints.

#include <crucible/cog/CogIdentity.h>
#include <crucible/ir001/Comm.h>
#include <crucible/forge/recipes/Network.h>
#include <crucible/mimic/CogMimic.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::mimic::_wip::network {

namespace cog = ::crucible::cog;
namespace ir = ::crucible::forge::ir001;
namespace net = ::crucible::forge::recipes;

namespace wip_source {
struct CpuNetwork {};
struct NvNetwork {};
struct AmNetwork {};
struct IntelNetwork {};
struct MellanoxNetwork {};
struct BroadcomNetwork {};
}  // namespace wip_source

enum class NetworkBackendVendor : std::uint8_t {
    Cpu = 0,
    Nv,
    Am,
    Intel,
    Mellanox,
    Broadcom,
};

enum class NetworkArtifactKind : std::uint8_t {
    SocketOracle = 0,
    CudaAwareRdma,
    RocmAwareRdma,
    IntelIpuRdma,
    DpuOffload,
    SwitchPipeline,
};

enum class NetworkBackendError : std::uint8_t {
    None = 0,
    BackendUnavailable,
    UnsupportedCogKind,
    UnsupportedOperation,
    RecipeForbidsAlgorithm,
    EmptyContentHash,
};

[[nodiscard]] constexpr std::string_view
network_backend_vendor_name(NetworkBackendVendor vendor) noexcept {
    switch (vendor) {
        case NetworkBackendVendor::Cpu: return "cpu";
        case NetworkBackendVendor::Nv: return "nv";
        case NetworkBackendVendor::Am: return "am";
        case NetworkBackendVendor::Intel: return "intel";
        case NetworkBackendVendor::Mellanox: return "mellanox";
        case NetworkBackendVendor::Broadcom: return "broadcom";
        default: return "<unknown NetworkBackendVendor>";
    }
}

[[nodiscard]] constexpr std::string_view
network_artifact_kind_name(NetworkArtifactKind kind) noexcept {
    switch (kind) {
        case NetworkArtifactKind::SocketOracle: return "socket-oracle";
        case NetworkArtifactKind::CudaAwareRdma: return "cuda-aware-rdma";
        case NetworkArtifactKind::RocmAwareRdma: return "rocm-aware-rdma";
        case NetworkArtifactKind::IntelIpuRdma: return "intel-ipu-rdma";
        case NetworkArtifactKind::DpuOffload: return "dpu-offload";
        case NetworkArtifactKind::SwitchPipeline: return "switch-pipeline";
        default: return "<unknown NetworkArtifactKind>";
    }
}

[[nodiscard]] constexpr std::string_view
network_backend_error_name(NetworkBackendError error) noexcept {
    switch (error) {
        case NetworkBackendError::None: return "None";
        case NetworkBackendError::BackendUnavailable:
            return "BackendUnavailable";
        case NetworkBackendError::UnsupportedCogKind:
            return "UnsupportedCogKind";
        case NetworkBackendError::UnsupportedOperation:
            return "UnsupportedOperation";
        case NetworkBackendError::RecipeForbidsAlgorithm:
            return "RecipeForbidsAlgorithm";
        case NetworkBackendError::EmptyContentHash:
            return "EmptyContentHash";
        default: return "<unknown NetworkBackendError>";
    }
}

template <NetworkBackendVendor Vendor>
struct NetworkBackendTraits;

template <>
struct NetworkBackendTraits<NetworkBackendVendor::Cpu> {
    using source = wip_source::CpuNetwork;
    static constexpr NetworkArtifactKind artifact_kind =
        NetworkArtifactKind::SocketOracle;
};

template <>
struct NetworkBackendTraits<NetworkBackendVendor::Nv> {
    using source = wip_source::NvNetwork;
    static constexpr NetworkArtifactKind artifact_kind =
        NetworkArtifactKind::CudaAwareRdma;
};

template <>
struct NetworkBackendTraits<NetworkBackendVendor::Am> {
    using source = wip_source::AmNetwork;
    static constexpr NetworkArtifactKind artifact_kind =
        NetworkArtifactKind::RocmAwareRdma;
};

template <>
struct NetworkBackendTraits<NetworkBackendVendor::Intel> {
    using source = wip_source::IntelNetwork;
    static constexpr NetworkArtifactKind artifact_kind =
        NetworkArtifactKind::IntelIpuRdma;
};

template <>
struct NetworkBackendTraits<NetworkBackendVendor::Mellanox> {
    using source = wip_source::MellanoxNetwork;
    static constexpr NetworkArtifactKind artifact_kind =
        NetworkArtifactKind::DpuOffload;
};

template <>
struct NetworkBackendTraits<NetworkBackendVendor::Broadcom> {
    using source = wip_source::BroadcomNetwork;
    static constexpr NetworkArtifactKind artifact_kind =
        NetworkArtifactKind::SwitchPipeline;
};

template <NetworkBackendVendor Vendor, cog::CogKind Kind>
[[nodiscard]] consteval bool backend_accepts_cog_kind() noexcept {
    if constexpr (Vendor == NetworkBackendVendor::Cpu) {
        return Kind == cog::CogKind::CpuCore || Kind == cog::CogKind::CpuSocket;
    } else if constexpr (Vendor == NetworkBackendVendor::Nv) {
        return Kind == cog::CogKind::Gpu || Kind == cog::CogKind::NicPort;
    } else if constexpr (Vendor == NetworkBackendVendor::Am) {
        return Kind == cog::CogKind::Gpu || Kind == cog::CogKind::NicPort;
    } else if constexpr (Vendor == NetworkBackendVendor::Intel) {
        return Kind == cog::CogKind::CpuSocket || Kind == cog::CogKind::NicPort;
    } else if constexpr (Vendor == NetworkBackendVendor::Mellanox) {
        return Kind == cog::CogKind::NicPort || Kind == cog::CogKind::NvSwitch;
    } else if constexpr (Vendor == NetworkBackendVendor::Broadcom) {
        return Kind == cog::CogKind::NvSwitch || Kind == cog::CogKind::NicPort;
    } else {
        return false;
    }
}

template <ir::Ir001OpKind Kind>
inline constexpr bool ir001_network_kernel_kind_v =
    ir::Ir001PointToPointKind<Kind> || ir::Ir001CollectiveKind<Kind>;

template <NetworkBackendVendor Vendor, cog::CogKind Kind>
concept BackendAcceptsCog =
    backend_accepts_cog_kind<Vendor, Kind>();

template <class Node>
concept NetworkKernelNode =
    ir::Ir001NodeLike<Node> && ir001_network_kernel_kind_v<Node::kind>;

struct NetworkKernelArtifact {
    NetworkBackendVendor vendor = NetworkBackendVendor::Cpu;
    NetworkArtifactKind artifact_kind = NetworkArtifactKind::SocketOracle;
    ir::Ir001OpKind op_kind = ir::Ir001OpKind::AllReduce;
    net::NetworkCollectiveAlgorithm algorithm =
        net::NetworkCollectiveAlgorithm::Ring;
    net::NetworkEquivalenceClass equivalence =
        net::NetworkEquivalenceClass::OrderedTolerance;
    ContentHash content_hash{};
    std::uint64_t target_caps_class_hash = 0;
    std::uint64_t cog_kernel_cache_key = 0;
    std::uint32_t estimated_descriptor_bytes = 0;
    std::uint16_t participants = 1;
};

static_assert(std::is_trivially_copyable_v<NetworkKernelArtifact>);

template <NetworkBackendVendor Vendor>
using DeclaredNetworkKernel = safety::Tagged<
    NetworkKernelArtifact, typename NetworkBackendTraits<Vendor>::source>;

template <NetworkBackendVendor Vendor, class Kernel>
concept NetworkKernelFor =
    std::same_as<Kernel, DeclaredNetworkKernel<Vendor>>;

template <NetworkBackendVendor Vendor, cog::CogKind Kind>
struct NetworkBackend {
    static constexpr NetworkBackendVendor vendor = Vendor;
    static constexpr cog::CogKind cog_kind = Kind;
    static constexpr NetworkArtifactKind artifact_kind =
        NetworkBackendTraits<Vendor>::artifact_kind;
    static constexpr bool supported_cog =
        backend_accepts_cog_kind<Vendor, Kind>();
};

template <NetworkBackendVendor Vendor, cog::CogKind Kind, class Node>
concept NetworkBackendCanPlan =
    BackendAcceptsCog<Vendor, Kind> && NetworkKernelNode<Node>;

template <NetworkBackendVendor Vendor, cog::CogKind Kind, NetworkKernelNode Node>
    requires BackendAcceptsCog<Vendor, Kind>
[[nodiscard]] constexpr std::expected<DeclaredNetworkKernel<Vendor>,
                                      NetworkBackendError>
plan_network_kernel(
    CogMimic<Kind> const& mimic,
    ir::DeclaredIr001Node<Node> node,
    net::DeclaredNetworkRecipeConstraints constraints) noexcept {
    auto const& raw_node = node.value();
    if (raw_node.content_hash.raw() == 0) {
        return std::unexpected(NetworkBackendError::EmptyContentHash);
    }
    if (mimic.identity == nullptr || mimic.identity->uuid.is_zero()
        || mimic.identity->kind != Kind) {
        return std::unexpected(NetworkBackendError::UnsupportedCogKind);
    }

    auto const participants = []<class N>(N const& n) constexpr {
        if constexpr (std::same_as<typename N::attrs_type,
                                   ir::CollectiveAttrs>) {
            return n.attrs.participants.count.value();
        } else {
            return std::uint16_t{1};
        }
    }(raw_node);

    auto const count = net::admit_network_participant_count(participants);
    if (!count.has_value()) {
        return std::unexpected(NetworkBackendError::RecipeForbidsAlgorithm);
    }
    if constexpr (std::same_as<typename Node::attrs_type,
                               ir::CollectiveAttrs>) {
        if (auto ok = net::algorithm_eligible(
                constraints, raw_node.attrs.algorithm, *count);
            !ok.has_value()) {
            return std::unexpected(NetworkBackendError::RecipeForbidsAlgorithm);
        }
    }

    return DeclaredNetworkKernel<Vendor>{NetworkKernelArtifact{
        .vendor = Vendor,
        .artifact_kind = NetworkBackendTraits<Vendor>::artifact_kind,
        .op_kind = Node::kind,
        .algorithm = [&] constexpr {
            if constexpr (std::same_as<typename Node::attrs_type,
                                       ir::CollectiveAttrs>) {
                return raw_node.attrs.algorithm;
            } else {
                return net::NetworkCollectiveAlgorithm::Ring;
            }
        }(),
        .equivalence = constraints.value().equivalence,
        .content_hash = raw_node.content_hash,
        .target_caps_class_hash = mimic.target_caps_class_hash(),
        .cog_kernel_cache_key = mimic.cog_kernel_cache_key(),
        .estimated_descriptor_bytes =
            static_cast<std::uint32_t>(64U + 16U * participants),
        .participants = participants,
    }};
}

template <NetworkBackendVendor Vendor>
[[nodiscard]] constexpr std::expected<void, NetworkBackendError>
emit_network_kernel(DeclaredNetworkKernel<Vendor> const&) noexcept {
    return std::unexpected(NetworkBackendError::BackendUnavailable);
}

static_assert(sizeof(DeclaredNetworkKernel<NetworkBackendVendor::Cpu>)
              == sizeof(NetworkKernelArtifact));
static_assert(NetworkBackendCanPlan<NetworkBackendVendor::Cpu,
                                    cog::CogKind::CpuSocket,
                                    ir::AllReduceOp>);
static_assert(NetworkBackendCanPlan<NetworkBackendVendor::Nv,
                                    cog::CogKind::Gpu,
                                    ir::SendOp>);
static_assert(!BackendAcceptsCog<NetworkBackendVendor::Mellanox,
                                 cog::CogKind::Gpu>);

}  // namespace crucible::mimic::_wip::network
