#include <crucible/cntp/dataplane/Xdp.h>

#include "test_assert.h"

#include <cstdio>
#include <string_view>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace effects = crucible::effects;
namespace dataplane = crucible::cntp::dataplane;
namespace saf = crucible::safety;

namespace {

struct FlowKey {
    std::uint32_t src = 0;
    std::uint32_t dst = 0;

    [[nodiscard]] friend constexpr bool
    operator==(FlowKey, FlowKey) noexcept = default;
};

struct FlowDecision {
    dataplane::XdpAction action = dataplane::XdpAction::Pass;
    std::uint32_t queue = 0;
};

[[nodiscard]] cntp::NicInterfaceName iface() {
    auto parsed = cntp::NicInterfaceName::from("eth0");
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] cog::CogIdentity nic_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x130, 0x1};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NicPort;
    return id;
}

[[nodiscard]] cog::CogIdentity gpu_identity() {
    cog::CogIdentity id = nic_identity();
    id.kind = cog::CogKind::Gpu;
    return id;
}

[[nodiscard]] cog::NicPortTargetCaps native_caps() {
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::XdpNative);
    caps.features.set(cog::NicFeature::AfXdp);
    return caps;
}

void test_names_and_admission() {
    assert(dataplane::xdp_action_name(dataplane::XdpAction::Redirect) ==
           std::string_view{"Redirect"});
    assert(dataplane::xdp_mode_name(dataplane::XdpMode::Native) ==
           std::string_view{"Native"});
    assert(dataplane::xdp_program_kind_name(dataplane::XdpProgramKind::AfXdpRedirect) ==
           std::string_view{"AfXdpRedirect"});
    assert(dataplane::bpf_map_kind_name(dataplane::BpfMapKind::XskMap) ==
           std::string_view{"XskMap"});
    assert(dataplane::xdp_error_name(dataplane::XdpError::MissingNativeXdp) ==
           std::string_view{"MissingNativeXdp"});

    assert(!dataplane::admit_xdp_ifindex(0).has_value());
    assert(!dataplane::admit_bpf_map_entries(0).has_value());
    assert(!dataplane::admit_bpf_map_element_bytes(0).has_value());
    assert(dataplane::admit_xdp_ifindex(7).has_value());

    std::printf("  test_names_and_admission: PASSED\n");
}

void test_program_caps() {
    effects::ColdInitCtx init{};
    auto ifindex = dataplane::admit_xdp_ifindex(7);
    assert(ifindex.has_value());

    auto native = dataplane::mint_xdp_program(
        init, iface(), *ifindex, dataplane::XdpProgramKind::AfXdpRedirect,
        dataplane::XdpMode::Native);
    static_assert(std::same_as<decltype(native)::tag_type, saf::source::Xdp>);
    assert(native.value().required_features.test(cog::NicFeature::XdpNative));
    assert(dataplane::xdp_admit_nic(nic_identity(), native_caps(), native).has_value());

    cog::NicPortTargetCaps no_xdp{};
    auto rejected = dataplane::xdp_admit_nic(nic_identity(), no_xdp, native);
    assert(!rejected.has_value());
    assert(rejected.error() == dataplane::XdpError::MissingNativeXdp);

    auto wrong_cog = dataplane::xdp_admit_nic(gpu_identity(), native_caps(), native);
    assert(!wrong_cog.has_value());
    assert(wrong_cog.error() == dataplane::XdpError::WrongCogKind);

    std::printf("  test_program_caps: PASSED\n");
}

void test_map_spec_and_image() {
    auto entries = dataplane::admit_bpf_map_entries(2);
    assert(entries.has_value());
    auto spec = dataplane::mint_bpf_map_spec<FlowKey, FlowDecision>(
        dataplane::BpfMapKind::LruHash, *entries);
    assert(spec.has_value());
    static_assert(std::same_as<
                  std::remove_cvref_t<decltype(*spec)>::tag_type,
                  saf::source::BpfMap>);
    assert(spec->value().key_bytes.value() == sizeof(FlowKey));
    assert(spec->value().value_bytes.value() == sizeof(FlowDecision));

    dataplane::BpfMapImage<FlowKey, FlowDecision, 2, dataplane::BpfMapKind::LruHash> map{};
    assert(map.update(FlowKey{.src = 1, .dst = 2},
                      FlowDecision{.action = dataplane::XdpAction::Redirect,
                                   .queue = 7}).has_value());
    auto found = map.lookup(FlowKey{.src = 1, .dst = 2});
    assert(found.has_value());
    assert(found->action == dataplane::XdpAction::Redirect);
    assert(found->queue == 7);

    auto duplicate = map.update(FlowKey{.src = 1, .dst = 2},
                                FlowDecision{},
                                dataplane::BpfMapUpdate::NoExist);
    assert(!duplicate.has_value());
    assert(duplicate.error() == dataplane::XdpError::KeyAlreadyExists);

    assert(map.update(FlowKey{.src = 2, .dst = 3}, FlowDecision{}).has_value());
    auto full = map.update(FlowKey{.src = 3, .dst = 4}, FlowDecision{});
    assert(!full.has_value());
    assert(full.error() == dataplane::XdpError::MapFull);

    assert(map.erase(FlowKey{.src = 1, .dst = 2}).has_value());
    assert(!map.lookup(FlowKey{.src = 1, .dst = 2}).has_value());
    assert(map.lookup(FlowKey{.src = 2, .dst = 3}).has_value());

    std::printf("  test_map_spec_and_image: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(dataplane::XdpIfIndex) == sizeof(std::uint32_t));
    static_assert(sizeof(dataplane::DeclaredXdpProgram) == sizeof(dataplane::XdpProgramSpec));
    static_assert(sizeof(dataplane::DeclaredBpfMap) == sizeof(dataplane::BpfMapSpec));
    static_assert(!std::copy_constructible<
                  dataplane::BpfMapImage<FlowKey, FlowDecision, 2>>);
    static_assert(dataplane::BpfKey<FlowKey>);
    static_assert(!dataplane::BpfKey<FlowDecision>);
    static_assert(dataplane::BpfScalar<FlowDecision>);
    static_assert(dataplane::CtxFitsXdpMint<effects::ColdInitCtx>);
    static_assert(!dataplane::CtxFitsXdpMint<effects::BgDrainCtx>);

    std::printf("test_cntp_dataplane_xdp:\n");
    test_names_and_admission();
    test_program_caps();
    test_map_spec_and_image();
    std::printf("test_cntp_dataplane_xdp: all PASSED\n");
    return 0;
}
