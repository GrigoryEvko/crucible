#pragma once

// GAPS-168. Vendor-neutral Forge comm-phase substrate.
//
// This header deliberately does not implement a Forge pass manager, IR002
// KernelNode lowering, Mimic backend emission, verbs, eBPF, or runtime
// scheduling. It records the compile-time gates those later layers must pass:
// IR001 provenance, compute/comm pattern shape, recipe-tier legality, and
// combined resource-row fit against a Cog substrate.

#include <crucible/cog/FitsCog.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/forge/Ir001/Comm.h>
#include <crucible/forge/recipes/Network.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::forge::phases::comm {

namespace ir = ::crucible::forge::ir001;
namespace net = ::crucible::forge::recipes;

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
}  // namespace detail

enum class CommPhaseKind : std::uint8_t {
    Ingest = 0,
    Analyze,
    Rewrite,
    Fuse,
    LowerToKernels,
    Tile,
    Memplan,
    Compile,
    Schedule,
    Emit,
    Distribute,
    Validate,
};

enum class CommFusionPattern : std::uint8_t {
    SendFromEpilogue = 0,
    ReduceOnRecv,
    CompressBeforeSend,
    DecompressAfterRecv,
    ScatterFromAttention,
    PrefetchReceive,
};

enum class CommPhaseError : std::uint8_t {
    None = 0,
    PatternDisabled,
    RecipeForbidsPattern,
    RecipeForbidsAlgorithm,
};

struct CommPhasePolicy {
    bool send_from_epilogue = true;
    bool reduce_on_recv = true;
    bool compress_before_send = true;
    bool decompress_after_recv = true;
    bool scatter_from_attention = true;
    bool prefetch_receive = true;
};

struct FusedCommDecision {
    CommFusionPattern pattern = CommFusionPattern::SendFromEpilogue;
    ir::Ir001OpKind producer_kind = ir::Ir001OpKind::Gemm;
    ir::Ir001OpKind comm_kind = ir::Ir001OpKind::SendAsync;
    ContentHash producer_hash{};
    ContentHash comm_hash{};
    ContentHash fused_hash{};
    net::NetworkCollectiveAlgorithm algorithm =
        net::NetworkCollectiveAlgorithm::Ring;
    net::NetworkEquivalenceClass equivalence =
        net::NetworkEquivalenceClass::OrderedTolerance;
    std::uint16_t participants = 1;
};

using DeclaredFusedCommDecision =
    safety::Tagged<FusedCommDecision, safety::source::ForgeFused>;

[[nodiscard]] constexpr std::string_view
comm_phase_kind_name(CommPhaseKind phase) noexcept {
    switch (phase) {
        case CommPhaseKind::Ingest: return "Ingest";
        case CommPhaseKind::Analyze: return "Analyze";
        case CommPhaseKind::Rewrite: return "Rewrite";
        case CommPhaseKind::Fuse: return "Fuse";
        case CommPhaseKind::LowerToKernels: return "LowerToKernels";
        case CommPhaseKind::Tile: return "Tile";
        case CommPhaseKind::Memplan: return "Memplan";
        case CommPhaseKind::Compile: return "Compile";
        case CommPhaseKind::Schedule: return "Schedule";
        case CommPhaseKind::Emit: return "Emit";
        case CommPhaseKind::Distribute: return "Distribute";
        case CommPhaseKind::Validate: return "Validate";
        default: return "<unknown CommPhaseKind>";
    }
}

[[nodiscard]] constexpr std::string_view
comm_fusion_pattern_name(CommFusionPattern pattern) noexcept {
    switch (pattern) {
        case CommFusionPattern::SendFromEpilogue: return "SendFromEpilogue";
        case CommFusionPattern::ReduceOnRecv: return "ReduceOnRecv";
        case CommFusionPattern::CompressBeforeSend:
            return "CompressBeforeSend";
        case CommFusionPattern::DecompressAfterRecv:
            return "DecompressAfterRecv";
        case CommFusionPattern::ScatterFromAttention:
            return "ScatterFromAttention";
        case CommFusionPattern::PrefetchReceive: return "PrefetchReceive";
        default: return "<unknown CommFusionPattern>";
    }
}

[[nodiscard]] constexpr std::string_view
comm_phase_error_name(CommPhaseError error) noexcept {
    switch (error) {
        case CommPhaseError::None: return "None";
        case CommPhaseError::PatternDisabled: return "PatternDisabled";
        case CommPhaseError::RecipeForbidsPattern:
            return "RecipeForbidsPattern";
        case CommPhaseError::RecipeForbidsAlgorithm:
            return "RecipeForbidsAlgorithm";
        default: return "<unknown CommPhaseError>";
    }
}

[[nodiscard]] constexpr bool pattern_enabled(
    CommPhasePolicy policy, CommFusionPattern pattern) noexcept {
    switch (pattern) {
        case CommFusionPattern::SendFromEpilogue:
            return policy.send_from_epilogue;
        case CommFusionPattern::ReduceOnRecv:
            return policy.reduce_on_recv;
        case CommFusionPattern::CompressBeforeSend:
            return policy.compress_before_send;
        case CommFusionPattern::DecompressAfterRecv:
            return policy.decompress_after_recv;
        case CommFusionPattern::ScatterFromAttention:
            return policy.scatter_from_attention;
        case CommFusionPattern::PrefetchReceive:
            return policy.prefetch_receive;
        default:
            return false;
    }
}

template <CommFusionPattern Pattern>
struct PatternTraits;

template <>
struct PatternTraits<CommFusionPattern::SendFromEpilogue> {
    static constexpr bool requires_compute_producer = true;
    static constexpr bool requires_collective_comm = false;
    static constexpr bool requires_point_to_point_comm = true;
    static constexpr bool lossy = false;
    static constexpr bool order_relaxing = false;
};

template <>
struct PatternTraits<CommFusionPattern::ReduceOnRecv> {
    static constexpr bool requires_compute_producer = false;
    static constexpr bool requires_collective_comm = true;
    static constexpr bool requires_point_to_point_comm = false;
    static constexpr bool lossy = false;
    static constexpr bool order_relaxing = true;
};

template <>
struct PatternTraits<CommFusionPattern::CompressBeforeSend> {
    static constexpr bool requires_compute_producer = true;
    static constexpr bool requires_collective_comm = false;
    static constexpr bool requires_point_to_point_comm = true;
    static constexpr bool lossy = true;
    static constexpr bool order_relaxing = false;
};

template <>
struct PatternTraits<CommFusionPattern::DecompressAfterRecv> {
    static constexpr bool requires_compute_producer = true;
    static constexpr bool requires_collective_comm = false;
    static constexpr bool requires_point_to_point_comm = true;
    static constexpr bool lossy = false;
    static constexpr bool order_relaxing = false;
};

template <>
struct PatternTraits<CommFusionPattern::ScatterFromAttention> {
    static constexpr bool requires_compute_producer = true;
    static constexpr bool requires_collective_comm = true;
    static constexpr bool requires_point_to_point_comm = false;
    static constexpr bool lossy = false;
    static constexpr bool order_relaxing = false;
};

template <>
struct PatternTraits<CommFusionPattern::PrefetchReceive> {
    static constexpr bool requires_compute_producer = false;
    static constexpr bool requires_collective_comm = false;
    static constexpr bool requires_point_to_point_comm = true;
    static constexpr bool lossy = false;
    static constexpr bool order_relaxing = false;
};

template <ir::Ir001OpKind Kind>
inline constexpr bool ir001_compute_kind_v =
    ir::ir001_op_category(Kind) == ir::Ir001OpCategory::Compute;

template <CommFusionPattern Pattern, ir::Ir001OpKind ComputeKind,
          ir::Ir001OpKind CommKind>
[[nodiscard]] consteval bool pattern_accepts_kind_pair() noexcept {
    using traits = PatternTraits<Pattern>;
    if constexpr (traits::requires_compute_producer
                  && !ir001_compute_kind_v<ComputeKind>) {
        return false;
    }
    if constexpr (traits::requires_collective_comm
                  && !ir::Ir001CollectiveKind<CommKind>) {
        return false;
    }
    if constexpr (traits::requires_point_to_point_comm
                  && !ir::Ir001PointToPointKind<CommKind>) {
        return false;
    }
    return true;
}

template <class Recipe, CommFusionPattern Pattern>
concept CommFusionRecipeAllowed =
    net::DeclaresNetworkRecipe<Recipe> &&
    !(PatternTraits<Pattern>::lossy
      && Recipe::determinism != ReductionDeterminism::UNORDERED
      && Recipe::determinism != ReductionDeterminism::ORDERED) &&
    !(PatternTraits<Pattern>::order_relaxing
      && Recipe::determinism == ReductionDeterminism::BITEXACT_STRICT);

template <class ComputeNode, class CommNode, CommFusionPattern Pattern,
          cog::CogKind Cog>
concept CommFusionEligible =
    ir::Ir001NodeLike<ComputeNode> &&
    ir::Ir001NodeLike<CommNode> &&
    effects::IsConcurrentRow<typename ComputeNode::row_type> &&
    effects::IsConcurrentRow<typename CommNode::row_type> &&
    effects::ConcurrentlySchedulable<typename ComputeNode::row_type,
                                     typename CommNode::row_type> &&
    pattern_accepts_kind_pair<Pattern, ComputeNode::kind, CommNode::kind>() &&
    cog::FitsCog<effects::concurrent_row_sum_t<
                     typename ComputeNode::row_type,
                     typename CommNode::row_type>,
                 Cog>;

[[nodiscard]] constexpr bool runtime_recipe_allows_pattern(
    net::DeclaredNetworkRecipeConstraints constraints,
    CommFusionPattern pattern) noexcept {
    auto const& raw = constraints.value();
    if (pattern == CommFusionPattern::CompressBeforeSend) {
        return raw.lossy_compression_allowed;
    }
    if (pattern == CommFusionPattern::ReduceOnRecv) {
        return raw.equivalence != net::NetworkEquivalenceClass::ByteIdentical;
    }
    return true;
}

template <CommFusionPattern Pattern, cog::CogKind Cog, class ComputeNode,
          class CommNode>
    requires CommFusionEligible<ComputeNode, CommNode, Pattern, Cog>
[[nodiscard]] constexpr std::expected<DeclaredFusedCommDecision,
                                      CommPhaseError>
admit_comm_fusion(
    ir::DeclaredIr001Node<ComputeNode> producer,
    ir::DeclaredIr001Node<CommNode> comm,
    net::DeclaredNetworkRecipeConstraints constraints,
    CommPhasePolicy policy = {}) noexcept {
    if (!pattern_enabled(policy, Pattern)) {
        return std::unexpected(CommPhaseError::PatternDisabled);
    }
    if (!runtime_recipe_allows_pattern(constraints, Pattern)) {
        return std::unexpected(CommPhaseError::RecipeForbidsPattern);
    }

    auto const& comm_node = comm.value();
    auto const participants = []<class Node>(Node const& node) constexpr {
        if constexpr (std::same_as<typename Node::attrs_type,
                                   ir::CollectiveAttrs>) {
            return node.attrs.participants.count.value();
        } else {
            return std::uint16_t{1};
        }
    }(comm_node);
    auto const count = net::admit_network_participant_count(participants);
    if (!count.has_value()) {
        return std::unexpected(CommPhaseError::RecipeForbidsAlgorithm);
    }
    if constexpr (std::same_as<typename CommNode::attrs_type,
                               ir::CollectiveAttrs>) {
        if (auto ok = net::algorithm_eligible(
                constraints, comm_node.attrs.algorithm, *count);
            !ok.has_value()) {
            return std::unexpected(CommPhaseError::RecipeForbidsAlgorithm);
        }
    }

    auto const producer_hash = producer.value().content_hash;
    auto const comm_hash = comm.value().content_hash;
    auto const fused_raw =
        detail::hash_mix(
            detail::hash_mix(producer_hash.raw(), comm_hash.raw()),
            std::to_underlying(Pattern));

    return DeclaredFusedCommDecision{FusedCommDecision{
        .pattern = Pattern,
        .producer_kind = ComputeNode::kind,
        .comm_kind = CommNode::kind,
        .producer_hash = producer_hash,
        .comm_hash = comm_hash,
        .fused_hash = ContentHash::from_raw(fused_raw == 0 ? 1 : fused_raw),
        .algorithm = [&] constexpr {
            if constexpr (std::same_as<typename CommNode::attrs_type,
                                       ir::CollectiveAttrs>) {
                return comm_node.attrs.algorithm;
            } else {
                return net::NetworkCollectiveAlgorithm::Ring;
            }
        }(),
        .equivalence = constraints.value().equivalence,
        .participants = participants,
    }};
}

static_assert(sizeof(DeclaredFusedCommDecision) == sizeof(FusedCommDecision));
static_assert(std::is_trivially_copyable_v<FusedCommDecision>);

}  // namespace crucible::forge::phases::comm
