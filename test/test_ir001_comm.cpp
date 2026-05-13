#include <crucible/forge/Ir001/Comm.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace ir = crucible::forge::ir001;

namespace {

crucible::TensorMeta tensor(crucible::ScalarType dtype) {
    crucible::TensorMeta meta{};
    meta.ndim = 1;
    meta.dtype = dtype;
    meta.sizes[0] = crucible::tensor_dim(1024);
    meta.strides[0] = crucible::tensor_dim(1);
    return meta;
}

crucible::NumericalRecipe recipe() {
    crucible::NumericalRecipe r{};
    r.out_dtype = crucible::ScalarType::Float;
    r.accum_dtype = crucible::ScalarType::Float;
    r.determinism = crucible::ReductionDeterminism::BITEXACT_TC;
    return r;
}

std::array<crucible::cog::CogIdentity, 2> peers() {
    std::array<crucible::cog::CogIdentity, 2> out{};
    out[0].uuid = crucible::cog::Uuid{1, 10};
    out[0].kind = crucible::cog::CogKind::Gpu;
    out[1].uuid = crucible::cog::Uuid{1, 11};
    out[1].kind = crucible::cog::CogKind::Gpu;
    return out;
}

void test_taxonomy() {
    static_assert(ir::kIr001OpKindCount == 72);
    static_assert(ir::Ir001CollectiveKind<ir::Ir001OpKind::AllReduce>);
    static_assert(!ir::Ir001CollectiveKind<ir::Ir001OpKind::SendAsync>);
    static_assert(ir::Ir001PointToPointKind<ir::Ir001OpKind::Put>);
    static_assert(!ir::Ir001PointToPointKind<ir::Ir001OpKind::Checkpoint>);

    auto all_reduce = ir::ir001_op_info(ir::Ir001OpKind::AllReduce);
    assert(all_reduce.category == ir::Ir001OpCategory::CollectiveSync);
    assert(all_reduce.name == std::string_view{"all_reduce"});
    assert(all_reduce.side_effecting);
    assert(all_reduce.network_visible);

    auto gemm = ir::ir001_op_info(ir::Ir001OpKind::Gemm);
    assert(gemm.category == ir::Ir001OpCategory::Compute);
    assert(!gemm.side_effecting);
    assert(!gemm.network_visible);

    std::printf("  test_taxonomy: PASSED\n");
}

void test_collective_node_admission() {
    auto p = peers();
    ir::AllReduceOp op{};
    op.attrs.input.meta = tensor(crucible::ScalarType::Float);
    op.attrs.output.meta = tensor(crucible::ScalarType::Float);
    op.attrs.participants.peers =
        ir::DeclaredPeerSet{std::span<const crucible::cog::CogIdentity>{p}};
    op.attrs.participants.count =
        ir::Ir001ParticipantCount{2,
                                  typename ir::Ir001ParticipantCount::Trusted{}};
    op.attrs.recipe = recipe();
    op.attrs.algorithm =
        crucible::forge::recipes::NetworkCollectiveAlgorithm::Ring;

    auto declared = ir::admit_ir001_node(op);
    auto const header = ir::serialize_ir001_header(declared);
    assert(header.kind == std::to_underlying(ir::Ir001OpKind::AllReduce));
    assert(header.flags == 1);
    assert(header.attr_words > 0);
    assert(header.content_hash != 0);

    bool visited = false;
    ir::visit_ir001_node(declared, [&](ir::AllReduceOp const& node) {
        visited = true;
        assert(node.content_hash.raw() == header.content_hash);
        assert(node.attrs.participants.count.value() == 2);
    });
    assert(visited);

    auto redeclared = ir::admit_ir001_node(declared.value());
    auto const reheader = ir::serialize_ir001_header(redeclared);
    assert(reheader.content_hash == header.content_hash);

    std::printf("  test_collective_node_admission: PASSED\n");
}

void test_other_attr_shapes() {
    static_assert(sizeof(ir::Ir001WireHeader) == 16);
    static_assert(std::is_trivially_copyable_v<ir::Ir001WireHeader>);
    static_assert(std::is_trivially_copyable_v<ir::CollectiveAttrs>);
    static_assert(std::is_trivially_copyable_v<ir::PointToPointAttrs>);
    static_assert(std::is_trivially_copyable_v<ir::BarrierAttrs>);

    ir::SendOp send{};
    send.attrs.payload.meta = tensor(crucible::ScalarType::BFloat16);
    send.attrs.timeout_ms =
        ir::Ir001TimeoutMs{25, typename ir::Ir001TimeoutMs::Trusted{}};
    auto declared_send = ir::admit_ir001_node(send);
    auto send_header = ir::serialize_ir001_header(declared_send);
    assert(send_header.kind == std::to_underlying(ir::Ir001OpKind::SendAsync));
    assert(send_header.flags == 1);

    ir::TelemetryEmitOp telemetry{};
    telemetry.attrs.row = crucible::RowHash::from_raw(0x1234);
    telemetry.attrs.value = 42;
    auto declared_telemetry = ir::admit_ir001_node(telemetry);
    auto telemetry_header = ir::serialize_ir001_header(declared_telemetry);
    assert(telemetry_header.kind
           == std::to_underlying(ir::Ir001OpKind::IntTelemetryEmit));
    assert(telemetry_header.content_hash != send_header.content_hash);

    std::printf("  test_other_attr_shapes: PASSED\n");
}

}  // namespace

int main() {
    std::printf("test_ir001_comm:\n");
    test_taxonomy();
    test_collective_node_admission();
    test_other_attr_shapes();
    std::printf("test_ir001_comm: all PASSED\n");
    return 0;
}
