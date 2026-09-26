#pragma once

#include <crucible/cog/FitsCog.h>
#include <crucible/forge/Ir001/Comm.h>
#include <crucible/forge/recipes/Network.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Concurrent.h>
#include <foundation/reflect/Hash.h>

#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>

namespace crucible::forge::_wip::phases::comm {

namespace ir = ::crucible::forge::ir001;
namespace net = ::crucible::forge::recipes;

namespace detail {
// The fused key folds through the one combine_ids body of the tree.
using ::foundation::reflect::combine_ids;
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
    net::NetworkCollectiveAlgorithm algorithm = net::NetworkCollectiveAlgorithm::Ring;
    net::NetworkEquivalenceClass equivalence = net::NetworkEquivalenceClass::OrderedTolerance;
    std::uint16_t participants = 1;
};

// A fusion decision is the output of the FUSE phase, so it carries that
// phase's lane: a consumer that demands a later phase refuses it.
using DeclaredFusedCommDecision = ::fixy::Tagged<FusedCommDecision, ::fixy::tags::source::ForgePhase<'D'>>;

[[nodiscard]] constexpr bool pattern_enabled(CommPhasePolicy policy, CommFusionPattern pattern) noexcept {
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

// What a fusion pattern needs of its two nodes, and what it does to the
// numbers it moves.
struct CommFusionShape {
    bool requires_compute_producer = false;
    bool requires_collective_comm = false;
    bool requires_point_to_point_comm = false;
    bool lossy = false;
    bool order_relaxing = false;
};

// The function is consteval and its default arm is unreachable, so a
// pattern with no row here is no constant expression and every gate that
// asks for its shape fails to compile.
[[nodiscard]] consteval CommFusionShape comm_fusion_shape(CommFusionPattern pattern) {
    switch (pattern) {
        case CommFusionPattern::SendFromEpilogue:
            return {.requires_compute_producer = true, .requires_point_to_point_comm = true};
        case CommFusionPattern::ReduceOnRecv:
            return {.requires_collective_comm = true, .order_relaxing = true};
        case CommFusionPattern::CompressBeforeSend:
            return {.requires_compute_producer = true, .requires_point_to_point_comm = true, .lossy = true};
        case CommFusionPattern::DecompressAfterRecv:
            return {.requires_compute_producer = true, .requires_point_to_point_comm = true};
        case CommFusionPattern::ScatterFromAttention:
            return {.requires_compute_producer = true, .requires_collective_comm = true};
        case CommFusionPattern::PrefetchReceive:
            return {.requires_point_to_point_comm = true};
        default:
            std::unreachable();
    }
}

template <CommFusionPattern Pattern, ir::Ir001OpKind ComputeKind, ir::Ir001OpKind CommKind>
[[nodiscard]] consteval bool pattern_accepts_kind_pair() noexcept {
    constexpr CommFusionShape shape = comm_fusion_shape(Pattern);
    constexpr bool producer_ok =
        !shape.requires_compute_producer || ir::ir001_op_category(ComputeKind) == ir::Ir001OpCategory::Compute;
    constexpr bool collective_ok = !shape.requires_collective_comm || ir::Ir001CollectiveKind<CommKind>;
    constexpr bool point_to_point_ok = !shape.requires_point_to_point_comm || ir::Ir001PointToPointKind<CommKind>;
    return producer_ok && collective_ok && point_to_point_ok;
}

template <class Recipe, CommFusionPattern Pattern>
concept CommFusionRecipeAllowed =
    net::DeclaresNetworkRecipe<Recipe>
    && !(comm_fusion_shape(Pattern).lossy && Recipe::determinism != ReductionDeterminism::UNORDERED
         && Recipe::determinism != ReductionDeterminism::ORDERED)
    && !(comm_fusion_shape(Pattern).order_relaxing && Recipe::determinism == ReductionDeterminism::BITEXACT_STRICT);

template <class ComputeNode, class CommNode, CommFusionPattern Pattern, cog::CogKind Cog>
concept CommFusionEligible =
    ir::Ir001NodeLike<ComputeNode> && ir::Ir001NodeLike<CommNode>
    && ::foundation::effects::IsConcurrentRow<typename ComputeNode::row_type>
    && ::foundation::effects::IsConcurrentRow<typename CommNode::row_type>
    && ::foundation::effects::ConcurrentlySchedulable<typename ComputeNode::row_type, typename CommNode::row_type>
    && pattern_accepts_kind_pair<Pattern, ComputeNode::kind, CommNode::kind>()
    && cog::FitsCog<
        ::foundation::effects::concurrent_row_sum_t<typename ComputeNode::row_type, typename CommNode::row_type>, Cog>;

[[nodiscard]] constexpr bool runtime_recipe_allows_pattern(net::DeclaredNetworkRecipeConstraints constraints,
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

template <CommFusionPattern Pattern, cog::CogKind Cog, class ComputeNode, class CommNode>
    requires CommFusionEligible<ComputeNode, CommNode, Pattern, Cog>
[[nodiscard]] constexpr std::expected<DeclaredFusedCommDecision, CommPhaseError>
admit_comm_fusion(ir::DeclaredIr001Node<ComputeNode> producer, ir::DeclaredIr001Node<CommNode> comm,
                  net::DeclaredNetworkRecipeConstraints constraints, CommPhasePolicy policy = {}) noexcept {
    if (!pattern_enabled(policy, Pattern)) {
        return std::unexpected(CommPhaseError::PatternDisabled);
    }
    if (!runtime_recipe_allows_pattern(constraints, Pattern)) {
        return std::unexpected(CommPhaseError::RecipeForbidsPattern);
    }

    // A point-to-point node has one peer and no algorithm of its own.
    auto const& comm_node = comm.value();
    std::uint16_t participants = 1;
    auto algorithm = net::NetworkCollectiveAlgorithm::Ring;
    if constexpr (std::same_as<typename CommNode::attrs_type, ir::CollectiveAttrs>) {
        participants = comm_node.attrs.participants.count.value();
        algorithm = comm_node.attrs.algorithm;
    }
    auto const count = net::admit_network_participant_count(participants);
    if (!count.has_value()) {
        return std::unexpected(CommPhaseError::RecipeForbidsAlgorithm);
    }
    if constexpr (std::same_as<typename CommNode::attrs_type, ir::CollectiveAttrs>) {
        if (!net::algorithm_eligible(constraints, algorithm, *count).has_value()) {
            return std::unexpected(CommPhaseError::RecipeForbidsAlgorithm);
        }
    }

    auto const producer_hash = producer.value().content_hash;
    auto const comm_hash = comm_node.content_hash;
    auto const fused_raw =
        detail::combine_ids(detail::combine_ids(producer_hash.raw(), comm_hash.raw()), std::to_underlying(Pattern));

    return ::fixy::mint_tagged<::fixy::tags::source::ForgePhase<'D'>>(FusedCommDecision{
        .pattern = Pattern,
        .producer_kind = ComputeNode::kind,
        .comm_kind = CommNode::kind,
        .producer_hash = producer_hash,
        .comm_hash = comm_hash,
        .fused_hash = ContentHash::from_raw(fused_raw == 0 ? 1 : fused_raw),
        .algorithm = algorithm,
        .equivalence = constraints.value().equivalence,
        .participants = participants,
    });
}

static_assert(sizeof(DeclaredFusedCommDecision) == sizeof(FusedCommDecision));
static_assert(std::is_trivially_copyable_v<FusedCommDecision>);

}  // namespace crucible::forge::_wip::phases::comm
