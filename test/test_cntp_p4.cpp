#include <crucible/cntp/P4.h>

#include <cassert>
#include <concepts>
#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cog = crucible::cog;
namespace eff = crucible::effects;
namespace p4 = crucible::cntp::p4;
namespace saf = crucible::safety;

namespace {

cog::CogIdentity switch_identity(cog::CogKind kind = cog::CogKind::NvSwitch) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x145, 0x4};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = kind;
    return id;
}

cog::NvSwitchTargetCaps p4_caps(std::uint32_t tcam_entries = 4096) {
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::P4);
    caps.tcam_entries = saf::Tagged<std::uint32_t, saf::source::Vendor>{
        tcam_entries};
    return caps;
}

p4::P4ProgramSpec program_spec(bool compiler_available = false,
                               bool allow_backend_compile = false,
                               bool allow_backend_deploy = false) {
    return p4::P4ProgramSpec{
        .program_id = *p4::admit_p4_program_id(0x145),
        .kind = p4::P4ProgramKind::IntTelemetry,
        .source_bytes = *p4::admit_p4_source_bytes(2048),
        .budget = *p4::admit_p4_resource_budget(128, 12, 64),
        .compiler_available = compiler_available,
        .allow_backend_compile = allow_backend_compile,
        .allow_backend_deploy = allow_backend_deploy,
    };
}

void test_admission_and_names() {
    assert(p4::p4_error_name(p4::P4Error::CompileDeferred)
           == std::string_view{"CompileDeferred"});
    assert(p4::p4_program_kind_name(p4::P4ProgramKind::SharpAssist)
           == std::string_view{"SharpAssist"});

    assert(p4::admit_p4_program_id(9).has_value());
    assert(!p4::admit_p4_program_id(0).has_value());
    assert(!p4::admit_p4_source_bytes(0).has_value());
    assert(!p4::admit_p4_tcam_entries(0).has_value());
    assert(!p4::admit_p4_stage_count(0).has_value());
    assert(!p4::admit_p4_register_width_bits(0).has_value());

    std::printf("  test_admission_and_names: PASSED\n");
}

void test_program_minting() {
    auto program = p4::mint_p4_program(
        eff::ColdInitCtx{}, switch_identity(), p4_caps(), program_spec());
    assert(program.has_value());
    assert(program->value().budget.tcam_entries.value() == 128);

    auto no_cap = p4_caps();
    no_cap.features.unset(cog::SwitchFeature::P4);
    auto missing_cap = p4::mint_p4_program(
        eff::ColdInitCtx{}, switch_identity(), no_cap, program_spec());
    assert(!missing_cap.has_value());
    assert(missing_cap.error() == p4::P4Error::MissingP4Capability);

    auto wrong_kind = p4::mint_p4_program(
        eff::ColdInitCtx{}, switch_identity(cog::CogKind::NicCard),
        p4_caps(), program_spec());
    assert(!wrong_kind.has_value());
    assert(wrong_kind.error() == p4::P4Error::NonSwitchCog);

    auto over_budget = p4::mint_p4_program(
        eff::ColdInitCtx{}, switch_identity(), p4_caps(64), program_spec());
    assert(!over_budget.has_value());
    assert(over_budget.error() == p4::P4Error::TcamBudgetExceeded);

    std::printf("  test_program_minting: PASSED\n");
}

void test_deploy_boundary() {
    auto compiler_missing = p4::mint_p4_program(
        eff::ColdInitCtx{}, switch_identity(), p4_caps(), program_spec());
    assert(compiler_missing.has_value());
    auto missing = p4::deploy_p4_program(
        switch_identity(), p4_caps(), *compiler_missing);
    assert(!missing.has_value());
    assert(missing.error() == p4::P4Error::CompilerUnavailable);

    auto compile_deferred = p4::mint_p4_program(
        eff::ColdInitCtx{}, switch_identity(), p4_caps(), program_spec(true));
    assert(compile_deferred.has_value());
    auto deferred = p4::deploy_p4_program(
        switch_identity(), p4_caps(), *compile_deferred);
    assert(!deferred.has_value());
    assert(deferred.error() == p4::P4Error::CompileDeferred);

    auto deploy_deferred = p4::mint_p4_program(
        eff::ColdInitCtx{}, switch_identity(), p4_caps(),
        program_spec(true, true));
    assert(deploy_deferred.has_value());
    auto deployment = p4::deploy_p4_program(
        switch_identity(), p4_caps(), *deploy_deferred);
    assert(!deployment.has_value());
    assert(deployment.error() == p4::P4Error::DeploymentDeferred);

    auto backend_plan = p4::mint_p4_program(
        eff::ColdInitCtx{}, switch_identity(), p4_caps(),
        program_spec(true, true, true));
    assert(backend_plan.has_value());
    auto backend = p4::force_p4_vendor_boundary(
        switch_identity(), p4_caps(), *backend_plan);
    assert(!backend.has_value());
    assert(backend.error() == p4::P4Error::VendorBackendUnavailable);

    std::printf("  test_deploy_boundary: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(p4::P4ProgramId) == sizeof(std::uint64_t));
    static_assert(sizeof(p4::P4SourceBytes) == sizeof(std::uint64_t));
    static_assert(sizeof(p4::P4TcamEntries) == sizeof(std::uint32_t));
    static_assert(sizeof(p4::DeclaredP4Program)
                  == sizeof(p4::P4ProgramSpec));
    static_assert(sizeof(p4::OwnedP4Deployment)
                  == sizeof(p4::P4DeploymentHandle));
    static_assert(std::same_as<
                  p4::DeclaredP4Program::tag_type,
                  saf::source::P4Compiled>);
    static_assert(p4::CtxFitsP4Mint<eff::ColdInitCtx>);
    static_assert(!p4::CtxFitsP4Mint<eff::BgDrainCtx>);
    static_assert(std::is_trivially_copyable_v<p4::P4ResourceBudget>);
    static_assert(std::is_trivially_copyable_v<p4::P4ProgramSpec>);
    static_assert(std::is_trivially_copyable_v<p4::P4DeploymentHandle>);

    std::printf("test_cntp_p4:\n");
    test_admission_and_names();
    test_program_minting();
    test_deploy_boundary();
    std::printf("test_cntp_p4: all PASSED\n");
    return 0;
}
