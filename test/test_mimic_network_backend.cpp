#include <crucible/mimic/_wip/am/network/Backend.h>
#include <crucible/mimic/_wip/broadcom/network/Backend.h>
#include <crucible/mimic/_wip/cpu/network/Backend.h>
#include <crucible/mimic/_wip/intel/network/Backend.h>
#include <crucible/mimic/_wip/mellanox/network/Backend.h>
#include <crucible/mimic/_wip/nv/network/Backend.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace ir = crucible::forge::ir001;
namespace net = crucible::forge::recipes;
namespace mb = crucible::mimic::_wip::network;

namespace {

template <cog::CogKind Kind>
crucible::mimic::CogMimic<Kind> mimic_for(cog::CogIdentity const& identity) {
    crucible::mimic::CogMimic<Kind> out{};
    out.identity = &identity;
    return out;
}

template <cog::CogKind Kind>
cog::CogIdentity identity_for(cog::Uuid uuid) {
    cog::CogIdentity identity{};
    identity.uuid = uuid;
    identity.kind = Kind;
    identity.level = cog::CogLevel::L0_Atomic;
    identity.firmware_revision =
        crucible::safety::Tagged<std::uint64_t,
                                 crucible::safety::source::Vendor>{7};
    return identity;
}

crucible::TensorMeta tensor() {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.dtype = crucible::ScalarType::Float;
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

std::array<cog::CogIdentity, 4> peers() {
    std::array<cog::CogIdentity, 4> out{};
    for (std::uint64_t i = 0; i < out.size(); ++i) {
        out[i].uuid = cog::Uuid{0x1111ULL, i + 1};
        out[i].kind = cog::CogKind::NicPort;
        out[i].level = cog::CogLevel::L0_Atomic;
    }
    return out;
}

ir::AllReduceOp all_reduce(std::span<const cog::CogIdentity> p,
                           net::NetworkCollectiveAlgorithm algorithm) {
    ir::AllReduceOp node{};
    node.attrs.input.meta = tensor();
    node.attrs.input.slot = crucible::SlotId{1};
    node.attrs.output.meta = tensor();
    node.attrs.output.slot = crucible::SlotId{2};
    node.attrs.participants.peers = ir::DeclaredPeerSet{p};
    node.attrs.participants.count =
        ir::Ir001ParticipantCount{static_cast<std::uint16_t>(p.size()),
                                  typename ir::Ir001ParticipantCount::Trusted{}};
    node.attrs.recipe = recipe(crucible::ReductionDeterminism::ORDERED);
    node.attrs.algorithm = algorithm;
    return node;
}

ir::SendOp send_node() {
    ir::SendOp node{};
    node.attrs.payload.meta = tensor();
    node.attrs.payload.slot = crucible::SlotId{3};
    node.attrs.peer = cog::CogIdentity{
        .uuid = cog::Uuid{0x2222ULL, 1},
        .level = cog::CogLevel::L0_Atomic,
        .kind = cog::CogKind::NicPort,
    };
    return node;
}

void test_static_contracts() {
    static_assert(sizeof(mb::DeclaredNetworkKernel<mb::NetworkBackendVendor::Nv>)
                  == sizeof(mb::NetworkKernelArtifact));
    static_assert(std::is_trivially_copyable_v<mb::NetworkKernelArtifact>);
    static_assert(mb::BackendAcceptsCog<mb::NetworkBackendVendor::Cpu,
                                        cog::CogKind::CpuSocket>);
    static_assert(mb::BackendAcceptsCog<mb::NetworkBackendVendor::Nv,
                                        cog::CogKind::Gpu>);
    static_assert(mb::BackendAcceptsCog<mb::NetworkBackendVendor::Mellanox,
                                        cog::CogKind::NicPort>);
    static_assert(!mb::BackendAcceptsCog<mb::NetworkBackendVendor::Broadcom,
                                         cog::CogKind::CpuSocket>);
    static_assert(std::same_as<
                  crucible::mimic::_wip::nv::network::Backend<cog::CogKind::Gpu>,
                  mb::NetworkBackend<mb::NetworkBackendVendor::Nv,
                                     cog::CogKind::Gpu>>);

    assert(mb::network_backend_vendor_name(mb::NetworkBackendVendor::Mellanox)
           == std::string_view{"mellanox"});
    assert(mb::network_artifact_kind_name(mb::NetworkArtifactKind::DpuOffload)
           == std::string_view{"dpu-offload"});
    assert(mb::network_backend_error_name(
               mb::NetworkBackendError::BackendUnavailable)
           == std::string_view{"BackendUnavailable"});

    std::printf("  test_static_contracts: PASSED\n");
}

void test_cpu_collective_plan() {
    auto p = peers();
    auto constraints = net::query_constraints(
        recipe(crucible::ReductionDeterminism::ORDERED),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto cpu_identity = identity_for<cog::CogKind::CpuSocket>(
        cog::Uuid{0xCAFEULL, 1});
    auto cpu = mimic_for<cog::CogKind::CpuSocket>(cpu_identity);
    auto planned = mb::plan_network_kernel<mb::NetworkBackendVendor::Cpu>(
        cpu,
        ir::admit_ir001_node(all_reduce(
            p, net::NetworkCollectiveAlgorithm::Ring)),
        constraints);

    assert(planned.has_value());
    assert(planned->value().vendor == mb::NetworkBackendVendor::Cpu);
    assert(planned->value().artifact_kind
           == mb::NetworkArtifactKind::SocketOracle);
    assert(planned->value().op_kind == ir::Ir001OpKind::AllReduce);
    assert(planned->value().participants == 4);
    assert(planned->value().content_hash.raw() != 0);
    assert(planned->value().target_caps_class_hash != 0);
    assert(planned->value().cog_kernel_cache_key != 0);

    auto emitted =
        mb::emit_network_kernel<mb::NetworkBackendVendor::Cpu>(*planned);
    assert(!emitted.has_value());
    assert(emitted.error() == mb::NetworkBackendError::BackendUnavailable);

    std::printf("  test_cpu_collective_plan: PASSED\n");
}

void test_gpu_point_to_point_plan() {
    auto constraints = net::query_constraints(
        recipe(crucible::ReductionDeterminism::ORDERED));
    auto gpu_identity = identity_for<cog::CogKind::Gpu>(
        cog::Uuid{0xCAFEULL, 2});
    auto gpu = mimic_for<cog::CogKind::Gpu>(gpu_identity);
    auto planned = mb::plan_network_kernel<mb::NetworkBackendVendor::Nv>(
        gpu, ir::admit_ir001_node(send_node()), constraints);

    assert(planned.has_value());
    assert(planned->value().vendor == mb::NetworkBackendVendor::Nv);
    assert(planned->value().artifact_kind
           == mb::NetworkArtifactKind::CudaAwareRdma);
    assert(planned->value().op_kind == ir::Ir001OpKind::SendAsync);
    assert(planned->value().participants == 1);

    std::printf("  test_gpu_point_to_point_plan: PASSED\n");
}

void test_recipe_rejection() {
    auto p = peers();
    auto strict = net::query_constraints(
        recipe(crucible::ReductionDeterminism::BITEXACT_STRICT),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    auto nic_identity = identity_for<cog::CogKind::NicPort>(
        cog::Uuid{0xCAFEULL, 3});
    auto nic = mimic_for<cog::CogKind::NicPort>(nic_identity);
    auto planned = mb::plan_network_kernel<mb::NetworkBackendVendor::Mellanox>(
        nic,
        ir::admit_ir001_node(all_reduce(
            p, net::NetworkCollectiveAlgorithm::Sharp)),
        strict);

    assert(!planned.has_value());
    assert(planned.error() == mb::NetworkBackendError::RecipeForbidsAlgorithm);

    std::printf("  test_recipe_rejection: PASSED\n");
}

void test_unbound_mimic_rejected() {
    auto p = peers();
    auto constraints = net::query_constraints(
        recipe(crucible::ReductionDeterminism::ORDERED),
        net::NetworkReductionLaws{.associative = true, .commutative = true});
    crucible::mimic::CogMimic<cog::CogKind::CpuSocket> unbound{};
    auto planned = mb::plan_network_kernel<mb::NetworkBackendVendor::Cpu>(
        unbound,
        ir::admit_ir001_node(all_reduce(
            p, net::NetworkCollectiveAlgorithm::Ring)),
        constraints);

    assert(!planned.has_value());
    assert(planned.error() == mb::NetworkBackendError::UnsupportedCogKind);

    std::printf("  test_unbound_mimic_rejected: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_mimic_network_backend:\n");
    test_static_contracts();
    test_cpu_collective_plan();
    test_gpu_point_to_point_plan();
    test_recipe_rejection();
    test_unbound_mimic_rejected();
    std::printf("test_mimic_network_backend: all PASSED\n");
    return 0;
}
