#include <crucible/cntp/Doca.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace doca = crucible::cntp::doca;
namespace eff = crucible::effects;
namespace saf = crucible::safety;

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

doca::DocaOffloadSpec offload_spec(bool runtime_loaded = false,
                                   bool allow_backend_deploy = false) {
    return doca::DocaOffloadSpec{
        .program_id = *doca::admit_doca_program_id(0xd0ca),
        .kind = doca::DocaOffloadKind::SwimGossip,
        .image_bytes = *doca::admit_doca_image_bytes(4096),
        .queue_depth = *doca::admit_doca_queue_depth(64),
        .runtime_loaded = runtime_loaded,
        .allow_backend_deploy = allow_backend_deploy,
    };
}

void test_admission_and_names() {
    assert(doca::doca_error_name(doca::DocaError::DeployDeferred)
           == std::string_view{"DeployDeferred"});
    assert(doca::doca_offload_kind_name(doca::DocaOffloadKind::FlowSteering)
           == std::string_view{"FlowSteering"});

    auto program = doca::admit_doca_program_id(7);
    assert(program.has_value());
    assert(program->value() == 7);
    assert(!doca::admit_doca_program_id(0).has_value());
    assert(!doca::admit_doca_image_bytes(0).has_value());
    assert(!doca::admit_doca_queue_depth(0).has_value());
    auto zero_payload = doca::admit_doca_payload_bytes(0);
    assert(!zero_payload.has_value());
    assert(zero_payload.error() == doca::DocaError::InvalidPayloadBytes);

    std::printf("  test_admission_and_names: PASSED\n");
}

void test_plan_minting() {
    auto plan = doca::mint_doca_deploy_plan(
        eff::ColdInitCtx{}, dpu_identity(), doca_caps(), offload_spec());
    assert(plan.has_value());
    assert(plan->value().spec.program_id.value() == 0xd0ca);

    auto no_cap = doca_caps();
    no_cap.features.unset(cog::SwitchFeature::Doca);
    auto missing_cap = doca::mint_doca_deploy_plan(
        eff::ColdInitCtx{}, dpu_identity(), no_cap, offload_spec());
    assert(!missing_cap.has_value());
    assert(missing_cap.error() == doca::DocaError::MissingDocaCapability);

    auto non_dpu = doca::mint_doca_deploy_plan(
        eff::ColdInitCtx{}, dpu_identity(cog::CogKind::Gpu), doca_caps(),
        offload_spec());
    assert(!non_dpu.has_value());
    assert(non_dpu.error() == doca::DocaError::NonDpuCog);

    auto zero = dpu_identity();
    zero.uuid = cog::Uuid{};
    auto zero_dpu = doca::mint_doca_deploy_plan(
        eff::ColdInitCtx{}, zero, doca_caps(), offload_spec());
    assert(!zero_dpu.has_value());
    assert(zero_dpu.error() == doca::DocaError::ZeroDpuCog);

    std::printf("  test_plan_minting: PASSED\n");
}

void test_deploy_boundary() {
    auto unavailable_plan = doca::mint_doca_deploy_plan(
        eff::ColdInitCtx{}, dpu_identity(), doca_caps(), offload_spec());
    assert(unavailable_plan.has_value());
    auto unavailable = doca::deploy_doca_offload(*unavailable_plan);
    assert(!unavailable.has_value());
    assert(unavailable.error() == doca::DocaError::RuntimeUnavailable);

    auto deferred_plan = doca::mint_doca_deploy_plan(
        eff::ColdInitCtx{}, dpu_identity(), doca_caps(), offload_spec(true));
    assert(deferred_plan.has_value());
    auto deferred = doca::deploy_doca_offload(*deferred_plan);
    assert(!deferred.has_value());
    assert(deferred.error() == doca::DocaError::DeployDeferred);

    auto backend_plan = doca::mint_doca_deploy_plan(
        eff::ColdInitCtx{}, dpu_identity(), doca_caps(),
        offload_spec(true, true));
    assert(backend_plan.has_value());
    auto backend = doca::force_doca_backend_boundary(*backend_plan);
    assert(!backend.has_value());
    assert(backend.error() == doca::DocaError::VendorBackendUnavailable);

    std::printf("  test_deploy_boundary: PASSED\n");
}

void test_comm_boundary() {
    doca::OwnedDocaOffload owned{doca::DocaOffloadHandle{
        .dpu_uuid = dpu_identity().uuid,
        .program_id = *doca::admit_doca_program_id(0xd0ca),
        .kind = doca::DocaOffloadKind::SwimGossip,
        .queue_depth = *doca::admit_doca_queue_depth(64),
    }};

    doca::DpuCommChannel channel{
        std::move(owned),
        doca::DocaChannelConfig{
            .max_payload_bytes = *doca::admit_doca_payload_bytes(4),
            .comm_channel_ready = false,
        },
    };
    std::array<std::byte, 8> bytes{};
    auto too_large = channel.send_to_dpu(eff::BgDrainCtx{}, bytes);
    assert(!too_large.has_value());
    assert(too_large.error() == doca::DocaError::PayloadTooLarge);

    auto unavailable = channel.send_to_dpu(
        eff::BgDrainCtx{}, std::span<const std::byte>{bytes.data(), 4});
    assert(!unavailable.has_value());
    assert(unavailable.error() == doca::DocaError::CommChannelUnavailable);

    std::printf("  test_comm_boundary: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(doca::DocaProgramId) == sizeof(std::uint64_t));
    static_assert(sizeof(doca::DocaImageBytes) == sizeof(std::uint64_t));
    static_assert(sizeof(doca::DocaQueueDepth) == sizeof(std::uint16_t));
    static_assert(sizeof(doca::DeclaredDocaDeployPlan)
                  == sizeof(doca::DocaDeployPlan));
    static_assert(sizeof(doca::OwnedDocaOffload)
                  == sizeof(doca::DocaOffloadHandle));
    static_assert(std::same_as<
                  doca::DeclaredDocaDeployPlan::tag_type,
                  saf::source::DocaOffload>);
    static_assert(doca::CtxFitsDocaMint<eff::ColdInitCtx>);
    static_assert(!doca::CtxFitsDocaMint<eff::BgDrainCtx>);
    static_assert(doca::CtxFitsDocaComm<eff::BgDrainCtx>);
    static_assert(!doca::CtxFitsDocaComm<eff::ColdInitCtx>);
    static_assert(std::is_trivially_copyable_v<doca::DocaDeployPlan>);
    static_assert(std::is_trivially_copyable_v<doca::DocaOffloadHandle>);

    std::printf("test_cntp_doca:\n");
    test_admission_and_names();
    test_plan_minting();
    test_deploy_boundary();
    test_comm_boundary();
    std::printf("test_cntp_doca: all PASSED\n");
    return 0;
}
