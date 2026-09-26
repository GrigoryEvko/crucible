#include <crucible/cntp/_wip/Doca.h>
#include <fixy/Ctx.h>

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace doca = crucible::cntp::_wip::doca;
namespace fe = ::foundation::effects;

namespace {

cog::CogIdentity dpu_identity(cog::CogKind kind = cog::CogKind::NicCard) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x144, 0xd0ca};
    id.level = cog::CogLevel::L1_Component;
    id.kind = kind;
    return id;
}

cog::NvSwitchTargetCaps doca_caps() {
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Doca);
    return caps;
}

doca::DocaOffloadSpec offload_spec(bool runtime_loaded = false, bool allow_backend_deploy = false) {
    return doca::DocaOffloadSpec{
        .program_id = *doca::admit_doca_program_id(0xd0ca),
        .kind = doca::DocaOffloadKind::SwimGossip,
        .image_bytes = *doca::admit_doca_image_bytes(4096),
        .queue_depth = *doca::admit_doca_queue_depth(64),
        .runtime_loaded = runtime_loaded,
        .allow_backend_deploy = allow_backend_deploy,
    };
}

doca::DocaChannelConfig channel_config(bool comm_channel_ready) {
    return doca::DocaChannelConfig{
        .max_payload_bytes = *doca::admit_doca_payload_bytes(4),
        .comm_channel_ready = comm_channel_ready,
    };
}

// The channel gate, read from the member templates.  A channel exists only
// for an offload that a DPU runs, so the gate is read without one.
template <class Ctx>
concept SendsToDpu = requires(doca::DpuCommChannel& channel, Ctx const& ctx, std::span<const std::byte> payload) {
    channel.send_to_dpu(ctx, payload);
};

template <class Ctx>
concept ReceivesFromDpu = requires(doca::DpuCommChannel& channel, Ctx const& ctx, std::span<std::byte> output) {
    channel.recv_from_dpu(ctx, output);
};

void test_admission_and_names() {
    assert(doca::doca_error_name(doca::DocaError::DeployDeferred) == std::string_view{"DeployDeferred"});
    assert(doca::doca_offload_kind_name(doca::DocaOffloadKind::FlowSteering) == std::string_view{"FlowSteering"});

    auto program = doca::admit_doca_program_id(7);
    assert(program.has_value());
    assert(program->value() == 7);
    assert(doca::admit_doca_program_id(0).error() == doca::DocaError::InvalidProgramId);
    assert(doca::admit_doca_image_bytes(0).error() == doca::DocaError::InvalidProgramImageBytes);
    assert(doca::admit_doca_queue_depth(0).error() == doca::DocaError::InvalidQueueDepth);
    auto zero_payload = doca::admit_doca_payload_bytes(0);
    assert(!zero_payload.has_value());
    assert(zero_payload.error() == doca::DocaError::InvalidPayloadBytes);

    std::printf("  test_admission_and_names: PASSED\n");
}

void test_plan_minting() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto plan = doca::mint_doca_deploy_plan(init, dpu_identity(), doca_caps(), offload_spec());
    assert(plan.has_value());
    assert(plan->value().spec.program_id.value() == 0xd0ca);

    auto switch_plan = doca::mint_doca_deploy_plan(init, dpu_identity(cog::CogKind::NvSwitch), doca_caps(), offload_spec());
    assert(switch_plan.has_value());

    auto no_cap = doca_caps();
    no_cap.features.unset(cog::SwitchFeature::Doca);
    auto missing_cap = doca::mint_doca_deploy_plan(init, dpu_identity(), no_cap, offload_spec());
    assert(!missing_cap.has_value());
    assert(missing_cap.error() == doca::DocaError::MissingDocaCapability);

    auto non_dpu = doca::mint_doca_deploy_plan(init, dpu_identity(cog::CogKind::Gpu), doca_caps(), offload_spec());
    assert(!non_dpu.has_value());
    assert(non_dpu.error() == doca::DocaError::NonDpuCog);

    auto zero = dpu_identity();
    zero.uuid = cog::Uuid{};
    auto zero_dpu = doca::mint_doca_deploy_plan(init, zero, doca_caps(), offload_spec());
    assert(!zero_dpu.has_value());
    assert(zero_dpu.error() == doca::DocaError::ZeroDpuCog);

    std::printf("  test_plan_minting: PASSED\n");
}

void test_deploy_boundary() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    auto unavailable_plan = doca::mint_doca_deploy_plan(init, dpu_identity(), doca_caps(), offload_spec());
    assert(unavailable_plan.has_value());
    auto unavailable = doca::deploy_doca_offload(*unavailable_plan);
    assert(!unavailable.has_value());
    assert(unavailable.error() == doca::DocaError::RuntimeUnavailable);

    auto deferred_plan = doca::mint_doca_deploy_plan(init, dpu_identity(), doca_caps(), offload_spec(true));
    assert(deferred_plan.has_value());
    auto deferred = doca::deploy_doca_offload(*deferred_plan);
    assert(!deferred.has_value());
    assert(deferred.error() == doca::DocaError::DeployDeferred);

    auto backend_plan = doca::mint_doca_deploy_plan(init, dpu_identity(), doca_caps(), offload_spec(true, true));
    assert(backend_plan.has_value());
    auto backend = doca::force_doca_backend_boundary(*backend_plan);
    assert(!backend.has_value());
    assert(backend.error() == doca::DocaError::VendorBackendUnavailable);

    std::printf("  test_deploy_boundary: PASSED\n");
}

void test_comm_checks() {
    std::array<std::byte, 8> bytes{};
    auto too_large = doca::validate_doca_send(channel_config(false), bytes);
    assert(!too_large.has_value());
    assert(too_large.error() == doca::DocaError::PayloadTooLarge);

    auto not_ready = doca::validate_doca_send(channel_config(false), std::span<const std::byte>{bytes.data(), 4});
    assert(!not_ready.has_value());
    assert(not_ready.error() == doca::DocaError::CommChannelUnavailable);
    assert(doca::validate_doca_send(channel_config(true), std::span<const std::byte>{bytes.data(), 4}).has_value());

    auto small_output = doca::validate_doca_recv(channel_config(true), std::span<std::byte>{bytes.data(), 2});
    assert(!small_output.has_value());
    assert(small_output.error() == doca::DocaError::OutputBufferTooSmall);
    auto recv_not_ready = doca::validate_doca_recv(channel_config(false), bytes);
    assert(!recv_not_ready.has_value());
    assert(recv_not_ready.error() == doca::DocaError::CommChannelUnavailable);
    assert(doca::validate_doca_recv(channel_config(true), bytes).has_value());

    static_assert(SendsToDpu<::fixy::BgDrainCtx> && ReceivesFromDpu<::fixy::BgDrainCtx>);
    static_assert(!SendsToDpu<::fixy::ColdInitCtx> && !ReceivesFromDpu<::fixy::ColdInitCtx>);
    static_assert(!SendsToDpu<::fixy::HotFgCtx> && !ReceivesFromDpu<::fixy::HotFgCtx>);

    std::printf("  test_comm_checks: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(doca::DocaProgramId) == sizeof(std::uint64_t));
    static_assert(sizeof(doca::DocaImageBytes) == sizeof(std::uint64_t));
    static_assert(sizeof(doca::DocaQueueDepth) == sizeof(std::uint16_t));
    static_assert(sizeof(doca::DeclaredDocaDeployPlan) == sizeof(doca::DocaDeployPlan));
    static_assert(std::same_as<doca::DeclaredDocaDeployPlan::tag_type, doca::wip_source::DocaOffload>);
    static_assert(doca::CtxFitsDocaMint<::fixy::ColdInitCtx>);
    static_assert(!doca::CtxFitsDocaMint<::fixy::BgDrainCtx>);
    static_assert(!doca::CtxFitsDocaMint<::fixy::HotFgCtx>);
    static_assert(doca::CtxFitsDocaComm<::fixy::BgDrainCtx>);
    static_assert(!doca::CtxFitsDocaComm<::fixy::ColdInitCtx>);
    static_assert(!std::is_default_constructible_v<doca::DeclaredDocaDeployPlan>);
    static_assert(!std::is_constructible_v<doca::DocaOffloadHandle, cog::Uuid, doca::DocaProgramId,
                                           doca::DocaOffloadKind, doca::DocaQueueDepth>);
    static_assert(!std::is_copy_constructible_v<doca::DocaOffloadHandle>);

    std::printf("test_cntp_doca:\n");
    test_admission_and_names();
    test_plan_minting();
    test_deploy_boundary();
    test_comm_checks();
    std::printf("test_cntp_doca: all PASSED\n");
    return 0;
}
