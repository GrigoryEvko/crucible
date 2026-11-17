#include <crucible/cntp/dataplane/Xdp.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Tags.h>
#include <foundation/effects/Ctx.h>

#include "test_assert.h"

#include <concepts>
#include <type_traits>
#include <utility>

namespace cntp = crucible::cntp;
namespace cog = crucible::cog;
namespace dataplane = crucible::cntp::dataplane;
namespace source = ::fixy::tags::source;

namespace {

struct FlowKey {
    std::uint32_t src = 0;
    std::uint32_t dst = 0;

    [[nodiscard]] friend constexpr bool operator==(FlowKey const&, FlowKey const&) noexcept = default;
};

struct FlowDecision {
    dataplane::XdpAction action = dataplane::XdpAction::Pass;
    std::uint32_t queue = 0;
};

using FourSlotImage = dataplane::BpfMapImage<FlowKey, FlowDecision, 4, dataplane::BpfMapKind::LruHash>;

// The value of a call that succeeds in this test.
template <class Value>
Value take(::fixy::Result<Value, dataplane::XdpError> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a call that fails in this test.
template <class Value>
dataplane::XdpError error_of(::fixy::Result<Value, dataplane::XdpError> result) {
    return std::move(result).err().expect("the call fails in this test");
}

[[nodiscard]] cntp::NicInterfaceName iface() {
    return cntp::NicInterfaceName::from("eth0").expect("eth0 is an interface name");
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
    return caps;
}

using SlotOfFour = ::fixy::Index<4>;

// The slot where the probes of key start in an image of four slots.
[[nodiscard]] SlotOfFour home_of(FlowKey const& key) {
    return SlotOfFour::check(dataplane::detail::key_hash(key) % 4u).expect("a hash modulo 4 is below 4");
}

// The first key after the key after, in the order of src, whose probes start
// at the slot home.
[[nodiscard]] FlowKey key_with_home(SlotOfFour home, FlowKey after) {
    for (std::uint32_t src = after.src + 1;; ++src) {
        FlowKey const key{.src = src, .dst = 0};
        if (home_of(key).raw() == home.raw()) {
            return key;
        }
    }
}

// The key before the first key of the order of src.
[[nodiscard]] FlowKey key_before_each() { return FlowKey{.src = 0, .dst = 1}; }

[[nodiscard]] std::uint32_t queue_of(FourSlotImage const& map, FlowKey const& key) {
    return map.lookup(key).expect("the image holds the key").queue;
}

void test_names_and_admission() {
    assert(dataplane::xdp_action_name(dataplane::XdpAction::Redirect) == "Redirect");
    assert(dataplane::xdp_mode_name(dataplane::XdpMode::Native) == "Native");
    assert(dataplane::xdp_program_kind_name(dataplane::XdpProgramKind::GossipMulticast) == "GossipMulticast");
    assert(dataplane::bpf_map_kind_name(dataplane::BpfMapKind::DevMap) == "DevMap");
    assert(dataplane::xdp_error_name(dataplane::XdpError::MissingNativeXdp) == "MissingNativeXdp");
    assert(dataplane::xdp_action_name(static_cast<dataplane::XdpAction>(0xFF)) == "<unknown XdpAction>");

    assert(error_of(dataplane::admit_xdp_ifindex(0)) == dataplane::XdpError::InvalidIfIndex);
    assert(error_of(dataplane::admit_bpf_map_entries(0)) == dataplane::XdpError::InvalidMapEntries);
    assert(error_of(dataplane::admit_bpf_map_element_bytes(0)) == dataplane::XdpError::InvalidMapElementSize);
    assert(take(dataplane::admit_xdp_ifindex(7)).value() == 7u);

    crucible::test::pass("  test_names_and_admission: PASSED\n");
}

void test_program_caps() {
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    dataplane::XdpIfIndex const ifindex = take(dataplane::admit_xdp_ifindex(7));

    auto const native = dataplane::mint_xdp_program(init, iface(), ifindex, dataplane::XdpProgramKind::FlowFilter,
                                                    dataplane::XdpMode::Native);
    static_assert(std::same_as<decltype(native)::tag_type, source::Xdp>);
    assert(native.value().required_features.test(cog::NicFeature::XdpNative));
    assert(dataplane::xdp_admit_nic(nic_identity(), native_caps(), native).is_ok());

    cog::NicPortTargetCaps const no_xdp{};
    assert(error_of(dataplane::xdp_admit_nic(nic_identity(), no_xdp, native)) == dataplane::XdpError::MissingNativeXdp);
    assert(error_of(dataplane::xdp_admit_nic(gpu_identity(), native_caps(), native))
           == dataplane::XdpError::WrongCogKind);

    auto const offload = dataplane::mint_xdp_program(init, iface(), ifindex, dataplane::XdpProgramKind::TcamAcl,
                                                     dataplane::XdpMode::Offload);
    assert(error_of(dataplane::xdp_admit_nic(nic_identity(), native_caps(), offload))
           == dataplane::XdpError::MissingOffloadXdp);

    auto const generic = dataplane::mint_xdp_program(init, iface(), ifindex, dataplane::XdpProgramKind::FlowFilter,
                                                     dataplane::XdpMode::Generic);
    assert(dataplane::xdp_admit_nic(nic_identity(), no_xdp, generic).is_ok());

    crucible::test::pass("  test_program_caps: PASSED\n");
}

void test_map_spec_and_image() {
    dataplane::PositiveMapEntries const entries = take(dataplane::admit_bpf_map_entries(2));
    auto const spec = dataplane::mint_bpf_map_spec<FlowKey, FlowDecision>(dataplane::BpfMapKind::LruHash, entries);
    static_assert(std::same_as<decltype(spec)::tag_type, source::BpfMap>);
    assert(spec.value().key_bytes.value() == sizeof(FlowKey));
    assert(spec.value().value_bytes.value() == sizeof(FlowDecision));
    assert(spec.value().max_entries.value() == 2u);

    dataplane::BpfMapImage<FlowKey, FlowDecision, 2, dataplane::BpfMapKind::LruHash> map{};
    FlowKey const first{.src = 1, .dst = 2};
    assert(map.update(first, FlowDecision{.action = dataplane::XdpAction::Redirect, .queue = 7}).is_ok());
    FlowDecision const found = map.lookup(first).expect("the image holds the first key");
    assert(found.action == dataplane::XdpAction::Redirect);
    assert(found.queue == 7u);

    assert(error_of(map.update(first, FlowDecision{}, dataplane::BpfMapUpdate::NoExist))
           == dataplane::XdpError::KeyAlreadyExists);
    assert(error_of(map.update(FlowKey{.src = 9, .dst = 9}, FlowDecision{}, dataplane::BpfMapUpdate::Exist))
           == dataplane::XdpError::KeyNotFound);
    assert(map.update(first, FlowDecision{.queue = 8}, dataplane::BpfMapUpdate::Exist).is_ok());
    assert(map.lookup(first).expect("the image holds the first key").queue == 8u);

    FlowKey const second{.src = 2, .dst = 3};
    assert(map.update(second, FlowDecision{}).is_ok());
    assert(map.is_full());
    assert(error_of(map.update(FlowKey{.src = 3, .dst = 4}, FlowDecision{})) == dataplane::XdpError::MapFull);

    assert(map.erase(first).is_ok());
    assert(map.lookup(first).is_none());
    assert(map.lookup(second).is_some());
    assert(error_of(map.erase(first)) == dataplane::XdpError::KeyNotFound);
    assert(map.size() == 1u);

    crucible::test::pass("  test_map_spec_and_image: PASSED\n");
}

// Three keys start their probes at the last slot, so they take the slots 3,
// 0 and 1, and the run wraps from the end to the start.  A fourth key starts
// at slot 0 and takes slot 2.  The erase of the first key moves each later
// key of the run back, and each key stays where a lookup finds it.
void test_erase_of_a_wrapped_run() {
    FlowKey const first = key_with_home(::fixy::index<3>(), key_before_each());
    FlowKey const second = key_with_home(::fixy::index<3>(), first);
    FlowKey const third = key_with_home(::fixy::index<3>(), second);
    FlowKey const fourth = key_with_home(::fixy::index<0>(), key_before_each());

    FourSlotImage map{};
    assert(map.update(first, FlowDecision{.queue = 1}).is_ok());
    assert(map.update(second, FlowDecision{.queue = 2}).is_ok());
    assert(map.update(third, FlowDecision{.queue = 3}).is_ok());
    assert(map.update(fourth, FlowDecision{.queue = 4}).is_ok());
    assert(map.is_full());

    assert(map.erase(first).is_ok());
    assert(map.size() == 3u);
    assert(map.lookup(first).is_none());
    assert(queue_of(map, second) == 2u);
    assert(queue_of(map, third) == 3u);
    assert(queue_of(map, fourth) == 4u);

    // The slot that the erase freed takes a new key, and a second erase in
    // the middle of the run keeps the keys after it.
    FlowKey const fifth = key_with_home(::fixy::index<1>(), key_before_each());
    assert(map.update(fifth, FlowDecision{.queue = 5}).is_ok());
    assert(map.erase(third).is_ok());
    assert(queue_of(map, second) == 2u);
    assert(queue_of(map, fourth) == 4u);
    assert(queue_of(map, fifth) == 5u);
    assert(map.lookup(third).is_none());

    assert(map.erase(second).is_ok());
    assert(map.erase(fourth).is_ok());
    assert(map.erase(fifth).is_ok());
    assert(map.size() == 0u);

    crucible::test::pass("  test_erase_of_a_wrapped_run: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(dataplane::XdpIfIndex) == sizeof(std::uint32_t));
    static_assert(sizeof(dataplane::DeclaredXdpProgram) == sizeof(dataplane::XdpProgramSpec));
    static_assert(sizeof(dataplane::DeclaredBpfMap) == sizeof(dataplane::BpfMapSpec));
    static_assert(!std::copy_constructible<FourSlotImage>);
    static_assert(dataplane::BpfKey<FlowKey>);
    static_assert(!dataplane::BpfKey<FlowDecision>);
    static_assert(dataplane::BpfScalar<FlowDecision>);
    static_assert(dataplane::CtxFitsXdpMint<::fixy::ColdInitCtx>);
    static_assert(dataplane::CtxFitsXdpMint<::fixy::InitLoadCtx>);
    static_assert(!dataplane::CtxFitsXdpMint<::fixy::BgDrainCtx>);
    static_assert(!dataplane::CtxFitsXdpMint<::fixy::HotFgCtx>);
    static_assert(dataplane::BpfMapElement<FlowDecision>);
    static_assert(!dataplane::BpfMapElement<::fixy::FixedArray<std::byte, 65536>>);

    ::fixy::report(::fixy::Sink::Out, "test_cntp_dataplane_xdp:\n");
    test_names_and_admission();
    test_program_caps();
    test_map_spec_and_image();
    test_erase_of_a_wrapped_run();
    crucible::test::pass("test_cntp_dataplane_xdp: all PASSED\n");
    return 0;
}
