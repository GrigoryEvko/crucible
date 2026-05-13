#include <crucible/forge/_wip/Phases/Comm.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace phase = crucible::forge::_wip::phases::comm;
namespace ir = crucible::forge::ir001;
namespace net = crucible::forge::recipes;

namespace {

using ComputeRow = crucible::effects::ConcurrentRow<
    crucible::effects::SmBudget<16>,
    crucible::effects::HbmBytes<4096>,
    crucible::effects::HbmBandwidth<4096>>;
using CommRow = crucible::effects::ConcurrentRow<
    crucible::effects::NvlinkBandwidth<4096>,
    crucible::effects::HbmBandwidth<4096>>;

using ComputeNode =
    ir::Ir001Node<ir::Ir001OpKind::Gemm, ir::TensorPort, ComputeRow>;
using SendNode =
    ir::Ir001Node<ir::Ir001OpKind::SendAsync, ir::PointToPointAttrs, CommRow>;
using RecvNode =
    ir::Ir001Node<ir::Ir001OpKind::RecvAsync, ir::PointToPointAttrs, CommRow>;
using AllReduceNode =
    ir::Ir001Node<ir::Ir001OpKind::AllReduce, ir::CollectiveAttrs, CommRow>;
using ReduceScatterNode =
    ir::Ir001Node<ir::Ir001OpKind::ReduceScatter, ir::CollectiveAttrs, CommRow>;

struct OrderedRecipe {
    static constexpr crucible::ReductionDeterminism determinism =
        crucible::ReductionDeterminism::ORDERED;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = true;
};

struct StrictRecipe {
    static constexpr crucible::ReductionDeterminism determinism =
        crucible::ReductionDeterminism::BITEXACT_STRICT;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = true;
};

crucible::TensorMeta tensor(crucible::ScalarType dtype) {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.dtype = dtype;
    meta.sizes[0] = crucible::tensor_dim(1024);
    meta.strides[0] = crucible::tensor_dim(1);
    return meta;
}

crucible::NumericalRecipe recipe(crucible::ReductionDeterminism det) {
    crucible::NumericalRecipe r{};
    r.out_dtype = crucible::ScalarType::Float;
    r.accum_dtype = crucible::ScalarType::Float;
    r.determinism = det;
    return r;
}

std::array<crucible::cog::CogIdentity, 4> peers() {
    std::array<crucible::cog::CogIdentity, 4> out{};
    for (std::uint64_t i = 0; i < out.size(); ++i) {
        out[i].uuid = crucible::cog::Uuid{7, i + 1};
        out[i].kind = crucible::cog::CogKind::Gpu;
    }
    return out;
}

ComputeNode compute_node() {
    ComputeNode node{};
    node.attrs.meta = tensor(crucible::ScalarType::Float);
    node.attrs.slot = crucible::SlotId{3};
    return node;
}

SendNode send_node() {
    SendNode node{};
    node.attrs.payload.meta = tensor(crucible::ScalarType::Float);
    node.attrs.payload.slot = crucible::SlotId{4};
    node.attrs.peer = crucible::cog::CogIdentity{
        .uuid = crucible::cog::Uuid{9, 1},
        .level = crucible::cog::CogLevel::L0_Atomic,
        .kind = crucible::cog::CogKind::NicPort,
    };
    return node;
}

RecvNode recv_node() {
    RecvNode node{};
    node.attrs.payload.meta = tensor(crucible::ScalarType::Float);
    node.attrs.payload.slot = crucible::SlotId{5};
    node.attrs.peer = crucible::cog::CogIdentity{
        .uuid = crucible::cog::Uuid{9, 2},
        .level = crucible::cog::CogLevel::L0_Atomic,
        .kind = crucible::cog::CogKind::NicPort,
    };
    return node;
}

template <class Node>
Node collective_node(std::span<const crucible::cog::CogIdentity> p,
                     net::NetworkCollectiveAlgorithm algorithm) {
    Node node{};
    node.attrs.input.meta = tensor(crucible::ScalarType::Float);
    node.attrs.input.slot = crucible::SlotId{6};
    node.attrs.output.meta = tensor(crucible::ScalarType::Float);
    node.attrs.output.slot = crucible::SlotId{7};
    node.attrs.participants.peers =
        ir::DeclaredPeerSet{p};
    node.attrs.participants.count =
        ir::Ir001ParticipantCount{4,
                                  typename ir::Ir001ParticipantCount::Trusted{}};
    node.attrs.recipe = recipe(crucible::ReductionDeterminism::ORDERED);
    node.attrs.algorithm = algorithm;
    return node;
}

void test_names_layout_and_static_gates() {
    static_assert(sizeof(phase::DeclaredFusedCommDecision)
                  == sizeof(phase::FusedCommDecision));
    static_assert(std::is_trivially_copyable_v<phase::FusedCommDecision>);

    assert(phase::comm_phase_kind_name(phase::CommPhaseKind::Fuse)
           == std::string_view{"Fuse"});
    assert(phase::comm_fusion_pattern_name(
               phase::CommFusionPattern::ScatterFromAttention)
           == std::string_view{"ScatterFromAttention"});
    assert(phase::comm_phase_error_name(
               phase::CommPhaseError::RecipeForbidsPattern)
           == std::string_view{"RecipeForbidsPattern"});

    static_assert(phase::CommFusionEligible<
                  ComputeNode, SendNode,
                  phase::CommFusionPattern::SendFromEpilogue,
                  crucible::cog::CogKind::Gpu>);
    static_assert(phase::CommFusionEligible<
                  ComputeNode, ReduceScatterNode,
                  phase::CommFusionPattern::ScatterFromAttention,
                  crucible::cog::CogKind::Gpu>);
    static_assert(!phase::CommFusionRecipeAllowed<
                  StrictRecipe,
                  phase::CommFusionPattern::CompressBeforeSend>);
    static_assert(phase::CommFusionRecipeAllowed<
                  OrderedRecipe,
                  phase::CommFusionPattern::CompressBeforeSend>);

    std::printf("  test_names_layout_and_static_gates: PASSED\n");
}

void test_send_from_epilogue_decision() {
    auto constraints = net::query_constraints(
        recipe(crucible::ReductionDeterminism::ORDERED),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto decision = phase::admit_comm_fusion<
        phase::CommFusionPattern::SendFromEpilogue,
        crucible::cog::CogKind::Gpu>(
            ir::admit_ir001_node(compute_node()),
            ir::admit_ir001_node(send_node()),
            constraints);
    assert(decision.has_value());
    assert(decision->value().pattern
           == phase::CommFusionPattern::SendFromEpilogue);
    assert(decision->value().producer_kind == ir::Ir001OpKind::Gemm);
    assert(decision->value().comm_kind == ir::Ir001OpKind::SendAsync);
    assert(decision->value().producer_hash.raw() != 0);
    assert(decision->value().comm_hash.raw() != 0);
    assert(decision->value().fused_hash.raw() != 0);
    assert(decision->value().participants == 1);

    phase::CommPhasePolicy disabled{};
    disabled.send_from_epilogue = false;
    auto blocked = phase::admit_comm_fusion<
        phase::CommFusionPattern::SendFromEpilogue,
        crucible::cog::CogKind::Gpu>(
            ir::admit_ir001_node(compute_node()),
            ir::admit_ir001_node(send_node()),
            constraints,
            disabled);
    assert(!blocked.has_value());
    assert(blocked.error() == phase::CommPhaseError::PatternDisabled);

    std::printf("  test_send_from_epilogue_decision: PASSED\n");
}

void test_recipe_and_algorithm_blocks() {
    auto ordered = net::query_constraints(
        recipe(crucible::ReductionDeterminism::ORDERED),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto strict = net::query_constraints(
        recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto p = peers();

    auto compressed = phase::admit_comm_fusion<
        phase::CommFusionPattern::CompressBeforeSend,
        crucible::cog::CogKind::Gpu>(
            ir::admit_ir001_node(compute_node()),
            ir::admit_ir001_node(send_node()),
            ordered);
    assert(compressed.has_value());

    auto strict_compressed = phase::admit_comm_fusion<
        phase::CommFusionPattern::CompressBeforeSend,
        crucible::cog::CogKind::Gpu>(
            ir::admit_ir001_node(compute_node()),
            ir::admit_ir001_node(send_node()),
            strict);
    assert(!strict_compressed.has_value());
    assert(strict_compressed.error()
           == phase::CommPhaseError::RecipeForbidsPattern);

    auto strict_reduce = phase::admit_comm_fusion<
        phase::CommFusionPattern::ReduceOnRecv,
        crucible::cog::CogKind::Gpu>(
            ir::admit_ir001_node(recv_node()),
            ir::admit_ir001_node(collective_node<AllReduceNode>(
                p,
                net::NetworkCollectiveAlgorithm::Ring)),
            strict);
    assert(!strict_reduce.has_value());
    assert(strict_reduce.error()
           == phase::CommPhaseError::RecipeForbidsPattern);

    auto odd_peers = collective_node<AllReduceNode>(
        p,
        net::NetworkCollectiveAlgorithm::RecursiveHalvingDoubling);
    odd_peers.attrs.participants.count =
        ir::Ir001ParticipantCount{3,
                                  typename ir::Ir001ParticipantCount::Trusted{}};
    auto bad_algorithm = phase::admit_comm_fusion<
        phase::CommFusionPattern::ReduceOnRecv,
        crucible::cog::CogKind::Gpu>(
            ir::admit_ir001_node(recv_node()),
            ir::admit_ir001_node(odd_peers),
            ordered);
    assert(!bad_algorithm.has_value());
    assert(bad_algorithm.error()
           == phase::CommPhaseError::RecipeForbidsAlgorithm);

    std::printf("  test_recipe_and_algorithm_blocks: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_forge_comm_phase:\n");
    test_names_layout_and_static_gates();
    test_send_from_epilogue_decision();
    test_recipe_and_algorithm_blocks();
    std::printf("test_forge_comm_phase: all PASSED\n");
    return 0;
}
