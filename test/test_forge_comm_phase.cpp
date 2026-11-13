#include <crucible/forge/_wip/Phases/Comm.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <concepts>
#include "test_assert.h"
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace phase = crucible::forge::_wip::phases::comm;
namespace ir = crucible::forge::ir001;
namespace net = crucible::forge::recipes;

namespace {

using ComputeRow =
    ::foundation::effects::ConcurrentRow<::foundation::effects::SmBudget<16>, ::foundation::effects::HbmBytes<4096>,
                                         ::foundation::effects::HbmBandwidth<4096>>;
using CommRow = ::foundation::effects::ConcurrentRow<::foundation::effects::NvlinkBandwidth<4096>,
                                                     ::foundation::effects::HbmBandwidth<4096>>;

using ComputeNode = ir::Ir001Node<ir::Ir001OpKind::Gemm, ir::TensorPort, ComputeRow>;
using SendNode = ir::Ir001Node<ir::Ir001OpKind::SendAsync, ir::PointToPointAttrs, CommRow>;
using RecvNode = ir::Ir001Node<ir::Ir001OpKind::RecvAsync, ir::PointToPointAttrs, CommRow>;
using AllReduceNode = ir::Ir001Node<ir::Ir001OpKind::AllReduce, ir::CollectiveAttrs, CommRow>;
using ReduceScatterNode = ir::Ir001Node<ir::Ir001OpKind::ReduceScatter, ir::CollectiveAttrs, CommRow>;

struct OrderedRecipe {
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::ORDERED;
    static constexpr bool associative = true;
    static constexpr bool commutative = true;
    static constexpr bool participant_count_power_of_two = true;
};

struct StrictRecipe {
    static constexpr crucible::ReductionDeterminism determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
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

::foundation::core::FixedArray<crucible::cog::CogIdentity, 4> peers() {
    ::foundation::core::FixedArray<crucible::cog::CogIdentity, 4> out{};
    for (auto const index : out.indices()) {
        out.at(index).uuid = crucible::cog::Uuid{7, index.raw() + 1};
        out.at(index).kind = crucible::cog::CogKind::Gpu;
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
Node collective_node(::fixy::View<crucible::cog::CogIdentity const> p, net::NetworkCollectiveAlgorithm algorithm) {
    Node node{};
    node.attrs.input.meta = tensor(crucible::ScalarType::Float);
    node.attrs.input.slot = crucible::SlotId{6};
    node.attrs.output.meta = tensor(crucible::ScalarType::Float);
    node.attrs.output.slot = crucible::SlotId{7};
    node.attrs.participants.peers = ::fixy::mint_tagged<::fixy::tags::source::Ir001>(p);
    node.attrs.participants.count = ::fixy::mint_refined<ir::kIr001ParticipantRange>(std::uint16_t{4});
    node.attrs.recipe = recipe(crucible::ReductionDeterminism::ORDERED);
    node.attrs.algorithm = algorithm;
    return node;
}

void test_names_layout_and_static_gates() {
    static_assert(sizeof(phase::DeclaredFusedCommDecision) == sizeof(phase::FusedCommDecision));
    static_assert(std::is_trivially_copyable_v<phase::FusedCommDecision>);

    static_assert(::foundation::reflect::enum_name(phase::CommPhaseKind::Fuse) == std::string_view{"Fuse"});
    static_assert(::foundation::reflect::enum_name(phase::CommFusionPattern::ScatterFromAttention)
                  == std::string_view{"ScatterFromAttention"});
    static_assert(::foundation::reflect::enum_name(phase::CommPhaseError::RecipeForbidsPattern)
                  == std::string_view{"RecipeForbidsPattern"});

    // The shape table decides which node kinds a pattern accepts.
    static_assert(phase::comm_fusion_shape(phase::CommFusionPattern::CompressBeforeSend).lossy);
    static_assert(phase::comm_fusion_shape(phase::CommFusionPattern::ReduceOnRecv).order_relaxing);
    static_assert(!phase::comm_fusion_shape(phase::CommFusionPattern::PrefetchReceive).requires_compute_producer);
    static_assert(std::same_as<phase::DeclaredFusedCommDecision::tag_type, ::fixy::tags::source::ForgePhase<'D'>>);

    static_assert(phase::CommFusionEligible<ComputeNode, SendNode, phase::CommFusionPattern::SendFromEpilogue,
                                            crucible::cog::CogKind::Gpu>);
    static_assert(
        phase::CommFusionEligible<ComputeNode, ReduceScatterNode, phase::CommFusionPattern::ScatterFromAttention,
                                  crucible::cog::CogKind::Gpu>);
    static_assert(!phase::CommFusionRecipeAllowed<StrictRecipe, phase::CommFusionPattern::CompressBeforeSend>);
    static_assert(phase::CommFusionRecipeAllowed<OrderedRecipe, phase::CommFusionPattern::CompressBeforeSend>);

    crucible::test::pass("  test_names_layout_and_static_gates: PASSED\n");
}

void test_send_from_epilogue_decision() {
    auto constraints = net::query_constraints(recipe(crucible::ReductionDeterminism::ORDERED),
                                              net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto decision = phase::admit_comm_fusion<phase::CommFusionPattern::SendFromEpilogue, crucible::cog::CogKind::Gpu>(
        ir::admit_ir001_node(compute_node()), ir::admit_ir001_node(send_node()), constraints);
    assert(decision.is_ok());
    phase::FusedCommDecision const fused = std::move(decision).expect("the pattern and the recipe admit it").value();
    assert(fused.pattern == phase::CommFusionPattern::SendFromEpilogue);
    assert(fused.producer_kind == ir::Ir001OpKind::Gemm);
    assert(fused.comm_kind == ir::Ir001OpKind::SendAsync);
    assert(fused.producer_hash.raw() != 0);
    assert(fused.comm_hash.raw() != 0);
    assert(fused.fused_hash.raw() != 0);
    assert(fused.participants == 1);
    // The fused key.  It folds the content keys of the two nodes, so the
    // layout of TensorMeta is part of it.
    assert(fused.fused_hash.raw() == 0x00e077f646606207ULL);

    phase::CommPhasePolicy disabled{};
    disabled.send_from_epilogue = false;
    auto blocked = phase::admit_comm_fusion<phase::CommFusionPattern::SendFromEpilogue, crucible::cog::CogKind::Gpu>(
        ir::admit_ir001_node(compute_node()), ir::admit_ir001_node(send_node()), constraints, disabled);
    assert(blocked.is_err());
    assert(blocked.err().value_or(phase::CommPhaseError::None) == phase::CommPhaseError::PatternDisabled);

    crucible::test::pass("  test_send_from_epilogue_decision: PASSED\n");
}

void test_recipe_and_algorithm_blocks() {
    auto ordered = net::query_constraints(recipe(crucible::ReductionDeterminism::ORDERED),
                                          net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto strict = net::query_constraints(recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
                                         net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto p = peers();

    auto compressed =
        phase::admit_comm_fusion<phase::CommFusionPattern::CompressBeforeSend, crucible::cog::CogKind::Gpu>(
            ir::admit_ir001_node(compute_node()), ir::admit_ir001_node(send_node()), ordered);
    assert(compressed.is_ok());

    auto strict_compressed =
        phase::admit_comm_fusion<phase::CommFusionPattern::CompressBeforeSend, crucible::cog::CogKind::Gpu>(
            ir::admit_ir001_node(compute_node()), ir::admit_ir001_node(send_node()), strict);
    assert(strict_compressed.is_err());
    assert(strict_compressed.err().value_or(phase::CommPhaseError::None)
           == phase::CommPhaseError::RecipeForbidsPattern);

    auto strict_reduce = phase::admit_comm_fusion<phase::CommFusionPattern::ReduceOnRecv, crucible::cog::CogKind::Gpu>(
        ir::admit_ir001_node(recv_node()),
        ir::admit_ir001_node(collective_node<AllReduceNode>(p.view(), net::NetworkCollectiveAlgorithm::Ring)), strict);
    assert(strict_reduce.is_err());
    assert(strict_reduce.err().value_or(phase::CommPhaseError::None) == phase::CommPhaseError::RecipeForbidsPattern);

    auto odd_peers =
        collective_node<AllReduceNode>(p.view(), net::NetworkCollectiveAlgorithm::RecursiveHalvingDoubling);
    odd_peers.attrs.participants.count = ::fixy::mint_refined<ir::kIr001ParticipantRange>(std::uint16_t{3});
    auto bad_algorithm = phase::admit_comm_fusion<phase::CommFusionPattern::ReduceOnRecv, crucible::cog::CogKind::Gpu>(
        ir::admit_ir001_node(recv_node()), ir::admit_ir001_node(odd_peers), ordered);
    assert(bad_algorithm.is_err());
    assert(bad_algorithm.err().value_or(phase::CommPhaseError::None) == phase::CommPhaseError::RecipeForbidsAlgorithm);

    crucible::test::pass("  test_recipe_and_algorithm_blocks: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_forge_comm_phase:\n");
    test_names_layout_and_static_gates();
    test_send_from_epilogue_decision();
    test_recipe_and_algorithm_blocks();
    crucible::test::pass("test_forge_comm_phase: all PASSED\n");
    return 0;
}
