#include <crucible/cntp/Tcam.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Ctx.h>

#include "test_assert.h"
#include <concepts>
#include <cstdio>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

// The backend-boundary entry point carries a CRUCIBLE_STUB deprecation
// so that production callers see at compile time that no vendor install
// path is attached yet.  A fixture is allowed to call it anyway, which
// is what the suppression below is for.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace cog = crucible::cog;
namespace tcam = crucible::cntp::tcam;

namespace {

::fixy::ColdInitCtx init_ctx() { return ::fixy::ColdInitCtx{::foundation::effects::testing::init()}; }

cog::CogIdentity nic_identity(cog::CogKind kind = cog::CogKind::NicPort) {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x148, 0x1};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = kind;
    return id;
}

cog::CogIdentity switch_identity() {
    cog::CogIdentity id{};
    id.uuid = cog::Uuid{0x148, 0x5};
    id.level = cog::CogLevel::L0_Atomic;
    id.kind = cog::CogKind::NvSwitch;
    return id;
}

cog::NicPortTargetCaps nic_caps(std::uint32_t entries = 1024) {
    cog::NicPortTargetCaps caps{};
    caps.features.set(cog::NicFeature::Tcam);
    caps.tcam_entries = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint32_t>(entries);
    return caps;
}

cog::NvSwitchTargetCaps switch_caps(std::uint32_t entries = 4096) {
    cog::NvSwitchTargetCaps caps{};
    caps.features.set(cog::SwitchFeature::Tcam);
    caps.tcam_entries = ::fixy::mint_tagged<::fixy::tags::source::Vendor, std::uint32_t>(entries);
    return caps;
}

tcam::TcamFlowRule flow_rule(tcam::FlowAction action = tcam::FlowAction::Drop, std::uint32_t redirect_ifindex = 0) {
    return tcam::TcamFlowRule{
        .rule_id = tcam::admit_tcam_rule_id(0x148).expect("the rule id is not zero"),
        .match =
            tcam::FiveTuple{
                .src_ipv4_be = 0x0a000001u,
                .dst_ipv4_be = 0x0a000002u,
                .src_port = 443,
                .dst_port = 8443,
                .protocol = 6,
                .src_prefix_bits = 32,
                .dst_prefix_bits = 32,
                .src_port_wildcard = false,
                .dst_port_wildcard = false,
            },
        .action =
            tcam::TcamFlowAction{
                .kind = action,
                .redirect_ifindex = redirect_ifindex,
                .dscp = tcam::admit_tcam_dscp(46).expect("46 is a DSCP value"),
            },
        .priority = *tcam::admit_tcam_priority(10),
        .audit_to_cipher = true,
    };
}

// The value of a call that succeeds in this test.
template <class Value>
Value take(::fixy::Result<Value, tcam::TcamError> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a call that fails in this test, or None for a call that
// succeeds.
template <class Value>
tcam::TcamError error_of(::fixy::Result<Value, tcam::TcamError> result) {
    return std::move(result).match([](Value) noexcept { return tcam::TcamError::None; },
                                   [](tcam::TcamError error) noexcept { return error; });
}

// The error of an install that fails in this test.  The rule of an install
// that succeeds ends here, and the error reads None.
tcam::TcamError error_of(::fixy::Result<tcam::OwnedTcamRule, tcam::TcamError> result) {
    return std::move(result).match(
        [](tcam::OwnedTcamRule rule) noexcept {
            drop(std::move(rule));
            return tcam::TcamError::None;
        },
        [](tcam::TcamError error) noexcept { return error; });
}

void test_admission_and_names() {
    assert(tcam::tcam_error_name(tcam::TcamError::CapacityExceeded) == std::string_view{"CapacityExceeded"});
    assert(tcam::flow_action_name(tcam::FlowAction::Mirror) == std::string_view{"Mirror"});
    assert(tcam::tcam_target_kind_name(tcam::TcamTargetKind::Switch) == std::string_view{"Switch"});
    assert(tcam::tcam_error_name(static_cast<tcam::TcamError>(200)) == std::string_view{"<unknown TcamError>"});

    assert(tcam::admit_tcam_rule_id(1).is_ok());
    assert(tcam::admit_tcam_rule_id(0).is_err());
    assert(tcam::admit_tcam_rule_id(0).err() == ::fixy::some(tcam::TcamError::InvalidRuleId));
    assert(tcam::admit_tcam_entries(1).is_ok());
    assert(tcam::admit_tcam_entries(0).is_err());
    assert(tcam::admit_tcam_entries(tcam::kMaxStaticTcamRules + 1).is_err());
    assert(tcam::admit_tcam_entries(0).err() == ::fixy::some(tcam::TcamError::InvalidEntryCount));
    assert(tcam::admit_tcam_dscp(63).is_ok());
    assert(tcam::admit_tcam_dscp(64).is_err());
    assert(tcam::admit_tcam_dscp(64).err() == ::fixy::some(tcam::TcamError::InvalidActionParameter));
    assert(tcam::admit_tcam_priority(std::numeric_limits<std::uint16_t>::max()).has_value());

    auto bad_redirect = tcam::declare_tcam_rule(flow_rule(tcam::FlowAction::Redirect));
    assert(error_of(std::move(bad_redirect)) == tcam::TcamError::InvalidActionParameter);

    auto bad_prefix = flow_rule();
    bad_prefix.match.src_prefix_bits = 33;
    auto bad_match = tcam::declare_tcam_rule(bad_prefix);
    assert(error_of(std::move(bad_match)) == tcam::TcamError::InvalidMatchParameter);

    crucible::test::pass("  test_admission_and_names: PASSED\n");
}

void test_table_minting() {
    auto table = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                       tcam::admit_tcam_entries(64).expect("64 entries fit"));
    tcam::DeclaredTcamTable const plan = take(std::move(table));
    assert(plan.value().target_kind() == tcam::TcamTargetKind::NicPort);
    assert(plan.value().capacity().value() == 64);
    assert(plan.value().target().uuid == nic_identity().uuid);
    assert(!plan.value().backend_ready());

    auto sw = tcam::mint_tcam_table(init_ctx(), switch_identity(), switch_caps(),
                                    tcam::admit_tcam_entries(128).expect("128 entries fit"));
    tcam::DeclaredTcamTable const switch_plan = take(std::move(sw));
    assert(switch_plan.value().target_kind() == tcam::TcamTargetKind::Switch);

    auto no_cap = nic_caps();
    no_cap.features.unset(cog::NicFeature::Tcam);
    auto missing = tcam::mint_tcam_table(init_ctx(), nic_identity(), no_cap,
                                         tcam::admit_tcam_entries(64).expect("64 entries fit"));
    assert(error_of(std::move(missing)) == tcam::TcamError::MissingTcamCapability);

    auto over = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(4),
                                      tcam::admit_tcam_entries(8).expect("8 entries fit"));
    assert(error_of(std::move(over)) == tcam::TcamError::CapacityExceeded);

    auto wrong = tcam::mint_tcam_table(init_ctx(), nic_identity(cog::CogKind::Gpu), nic_caps(),
                                       tcam::admit_tcam_entries(64).expect("64 entries fit"));
    assert(error_of(std::move(wrong)) == tcam::TcamError::WrongTargetKind);

    auto zero_target = nic_identity();
    zero_target.uuid = cog::Uuid{};
    auto zero = tcam::mint_tcam_table(init_ctx(), zero_target, nic_caps(),
                                      tcam::admit_tcam_entries(64).expect("64 entries fit"));
    assert(error_of(std::move(zero)) == tcam::TcamError::ZeroTargetCog);

    crucible::test::pass("  test_table_minting: PASSED\n");
}

void test_rule_table_lifecycle() {
    auto table_plan = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                            tcam::admit_tcam_entries(1).expect("1 entry fits"));

    tcam::TcamRules<4> table{take(std::move(table_plan))};
    tcam::DeclaredTcamFlowRule const declared = take(tcam::declare_tcam_rule(flow_rule()));

    tcam::OwnedTcamRule handle = take(table.add_rule(declared));
    assert(handle.peek().rule_id().value() == 0x148);
    assert(handle.peek().target_uuid() == nic_identity().uuid);
    assert(table.installed_rules() == 1);
    assert(table.available_rules_remaining() == 0);

    assert(error_of(table.add_rule(declared)) == tcam::TcamError::TableFull);

    assert(table.note_match(handle, 7).is_ok());
    std::uint64_t const count = take(table.query_counter(handle));
    assert(count == 7);
    std::uint64_t const overflow_delta = ::fixy::max_of<std::uint64_t> - count + 1u;
    assert(error_of(table.note_match(handle, overflow_delta)) == tcam::TcamError::CounterOverflow);

    assert(table.remove_rule(std::move(handle)).is_ok());
    assert(table.installed_rules() == 0);

    crucible::test::pass("  test_rule_table_lifecycle: PASSED\n");
}

// A table that owns fewer slots than its plan asks for reports only the
// rules that it can hold.
void test_slot_bound_caps_available_rules() {
    auto table_plan = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                            tcam::admit_tcam_entries(64).expect("64 entries fit"));

    tcam::TcamRules<2> table{take(std::move(table_plan))};
    assert(table.available_rules_remaining() == 2);

    tcam::DeclaredTcamFlowRule const declared = take(tcam::declare_tcam_rule(flow_rule()));
    tcam::OwnedTcamRule first = take(table.add_rule(declared));
    tcam::OwnedTcamRule second = take(table.add_rule(declared));
    assert(table.available_rules_remaining() == 0);
    assert(error_of(table.add_rule(declared)) == tcam::TcamError::TableFull);

    assert(table.remove_rule(std::move(first)).is_ok());
    assert(table.remove_rule(std::move(second)).is_ok());
    crucible::test::pass("  test_slot_bound_caps_available_rules: PASSED\n");
}

// Two tables put the same rule in the same slot at the same generation.
// The target of the plan is the one difference, and it keeps the handle of
// one table out of the other.  A slot that a removal freed takes the next
// generation.
void test_foreign_handle_and_generation() {
    auto nic_plan = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                          tcam::admit_tcam_entries(4).expect("4 entries fit"));
    auto sw_plan = tcam::mint_tcam_table(init_ctx(), switch_identity(), switch_caps(),
                                         tcam::admit_tcam_entries(4).expect("4 entries fit"));

    tcam::TcamRules<4> nic_table{take(std::move(nic_plan))};
    tcam::TcamRules<4> sw_table{take(std::move(sw_plan))};
    tcam::DeclaredTcamFlowRule const declared = take(tcam::declare_tcam_rule(flow_rule()));

    tcam::OwnedTcamRule nic_handle = take(nic_table.add_rule(declared));
    tcam::OwnedTcamRule sw_handle = take(sw_table.add_rule(declared));
    assert(nic_handle.peek().slot() == sw_handle.peek().slot());
    assert(nic_handle.peek().generation() == sw_handle.peek().generation());

    assert(error_of(nic_table.query_counter(sw_handle)) == tcam::TcamError::InvalidRuleHandle);

    tcam::TcamRuleHandle raw = std::move(nic_handle).consume();
    auto rewrapped = ::fixy::mint_linear<tcam::TcamRuleHandle>(std::move(raw));
    assert(nic_table.remove_rule(std::move(rewrapped)).is_ok());

    tcam::OwnedTcamRule again = take(nic_table.add_rule(declared));
    assert(again.peek().generation() == 2);

    assert(sw_table.remove_rule(std::move(sw_handle)).is_ok());
    assert(nic_table.remove_rule(std::move(again)).is_ok());
    crucible::test::pass("  test_foreign_handle_and_generation: PASSED\n");
}

// One slot gives each generation one time.  A table with an 8-bit
// generation word runs its one slot through the word.  Each install gets a
// larger generation than the install before it, and the slot retires when
// its word has no next value.
void test_generation_never_comes_back() {
    auto table_plan = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                            ::fixy::mint_refined<tcam::tcam_entry_range>(std::uint32_t{1}));

    tcam::TcamRules<1, std::uint8_t> table{take(std::move(table_plan))};
    tcam::DeclaredTcamFlowRule const declared = take(tcam::declare_tcam_rule(flow_rule()));

    std::uint64_t last_generation = 0;
    std::uint32_t installs = 0;
    for (std::uint32_t round = 0; round < 1024; ++round) {
        auto installed = table.add_rule(declared);
        if (installed.is_err()) {
            assert(error_of(std::move(installed)) == tcam::TcamError::TableFull);
            break;
        }
        tcam::OwnedTcamRule handle = take(std::move(installed));
        assert(handle.peek().generation() > last_generation);
        last_generation = handle.peek().generation();
        ++installs;
        assert(table.remove_rule(std::move(handle)).is_ok());
    }
    assert(installs == 255);
    assert(last_generation == 255);
    assert(table.installed_rules() == 0);
    assert(table.available_rules_remaining() == 0);

    crucible::test::pass("  test_generation_never_comes_back: PASSED\n");
}

// The ready and the not-ready plan must produce different outcomes.
// Returning the unavailable error for both would collapse two distinct
// states into one, and every caller would read the ready case as a
// failure.
void test_backend_boundary() {
    tcam::DeclaredTcamFlowRule const declared = take(tcam::declare_tcam_rule(flow_rule()));

    auto ready_plan = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                            tcam::admit_tcam_entries(4).expect("4 entries fit"), true);
    tcam::DeclaredTcamTable const ready_table = take(std::move(ready_plan));
    assert(tcam::force_tcam_backend_boundary(ready_table, declared).is_ok());

    auto pending_plan = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                              tcam::admit_tcam_entries(4).expect("4 entries fit"), false);
    tcam::DeclaredTcamTable const pending_table = take(std::move(pending_plan));
    assert(error_of(tcam::force_tcam_backend_boundary(pending_table, declared))
           == tcam::TcamError::VendorBackendUnavailable);

    crucible::test::pass("  test_backend_boundary: PASSED\n");
}

// Nothing in this substrate reaches hardware.  The ready flag on a
// table plan is an administrator's claim that installing would be
// allowed, not evidence that any rule was programmed into silicon, and
// the two are easy to confuse because the ready path returns success.
// The separate compile-time marker is what carries the wire-level
// claim, and it stays false until a real install path is attached
// through one of the vendor or kernel interfaces.
//
// So the rule table here is a value, and its installed count reflects
// only the additions made in this process.  A real device could hold
// rules from a previous boot that a freshly built table would report as
// zero.  Wiring a backend means flipping the marker in the same change,
// programming the device on the ready path, and replacing this fixture
// with one that round-trips a rule through hardware and reads back its
// counters.
void test_apply_paths_are_stubbed() {
    static_assert(tcam::vendor_backend_attached == false,
                  "The TCAM substrate has no vendor backend.  Flipping "
                  "vendor_backend_attached to true requires a real install path, "
                  "a backend boundary that programs the device, and live-fabric "
                  "fixtures in place of this one.");
    static_assert(std::is_same_v<decltype(tcam::vendor_backend_attached), const bool>,
                  "the honesty marker must be a compile-time bool");

    tcam::DeclaredTcamFlowRule const declared = take(tcam::declare_tcam_rule(flow_rule()));

    // Success here is a transition inside this process and nothing
    // more.
    auto ready_plan = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                            tcam::admit_tcam_entries(4).expect("4 entries fit"), true);
    tcam::DeclaredTcamTable const ready_table = take(std::move(ready_plan));
    assert(tcam::force_tcam_backend_boundary(ready_table, declared).is_ok());

    // A fresh table starts empty whatever the device holds.
    tcam::TcamRules<4> table{ready_table};
    assert(table.installed_rules() == 0);
    assert(table.available_rules_remaining() == 4);
    tcam::OwnedTcamRule handle = take(table.add_rule(declared));
    assert(table.installed_rules() == 1);

    // The counter reads back whatever was written to it here.  On real
    // silicon it would carry match counts from the fabric.
    assert(take(table.query_counter(handle)) == 0);
    assert(table.note_match(handle, 42u).is_ok());
    assert(take(table.query_counter(handle)) == 42u);

    assert(table.remove_rule(std::move(handle)).is_ok());
    assert(table.installed_rules() == 0);

    // The unavailable error must stay reachable, or the ready path
    // would be the only outcome the code can produce.
    auto pending_plan = tcam::mint_tcam_table(init_ctx(), nic_identity(), nic_caps(),
                                              tcam::admit_tcam_entries(4).expect("4 entries fit"), false);
    tcam::DeclaredTcamTable const pending_table = take(std::move(pending_plan));
    assert(error_of(tcam::force_tcam_backend_boundary(pending_table, declared))
           == tcam::TcamError::VendorBackendUnavailable);

    tcam::TcamRules<4> pending_rules{pending_table};
    assert(error_of(pending_rules.require_backend_ready()) == tcam::TcamError::VendorBackendUnavailable);

    crucible::test::pass("  test_apply_paths_are_stubbed: PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(tcam::TcamRuleId) == sizeof(std::uint64_t));
    static_assert(sizeof(tcam::TcamEntryCount) == sizeof(std::uint32_t));
    static_assert(sizeof(tcam::TcamPriority) == sizeof(std::uint16_t));
    static_assert(sizeof(tcam::TcamDscp) == sizeof(std::uint8_t));
    static_assert(sizeof(tcam::DeclaredTcamFlowRule) == sizeof(tcam::TcamFlowRule));
    static_assert(::fixy::qtt_consume_tracked || sizeof(tcam::OwnedTcamRule) == sizeof(tcam::TcamRuleHandle));
    static_assert(std::same_as<tcam::DeclaredTcamFlowRule::tag_type, ::fixy::tags::source::TcamFlowRule>);
    static_assert(std::same_as<tcam::DeclaredTcamTable::tag_type, ::fixy::tags::source::TcamTable>);
    static_assert(tcam::TcamTableShape<1>);
    static_assert(!tcam::TcamTableShape<0>);
    static_assert(tcam::CtxFitsTcamMint<::fixy::ColdInitCtx, cog::NicPortTargetCaps>);
    static_assert(!tcam::CtxFitsTcamMint<::fixy::BgDrainCtx, cog::NicPortTargetCaps>);
    static_assert(!tcam::CtxFitsTcamMint<::fixy::ColdInitCtx, cog::GpuTargetCaps>);
    static_assert(!std::is_default_constructible_v<tcam::DeclaredTcamTable>);
    static_assert(!std::is_default_constructible_v<tcam::TcamRuleHandle>);
    static_assert(!std::is_copy_constructible_v<tcam::OwnedTcamRule>);
    static_assert(std::is_trivially_copyable_v<tcam::FiveTuple>);
    static_assert(!std::is_trivially_copyable_v<tcam::TcamFlowRule>);

    static_assert(!tcam::vendor_backend_attached, "The TCAM substrate is a documented stub. The backend boundary "
                                                  "reports success only when the table plan says installing is "
                                                  "allowed, which is an administrator's claim and not a wire-level "
                                                  "one. The vendor_backend_attached marker carries the wire-level "
                                                  "claim and stays false until a real install path is wired in.");

    ::fixy::report(::fixy::Sink::Out, "test_cntp_tcam:\n");
    test_admission_and_names();
    test_table_minting();
    test_rule_table_lifecycle();
    test_slot_bound_caps_available_rules();
    test_foreign_handle_and_generation();
    test_generation_never_comes_back();
    test_backend_boundary();
    test_apply_paths_are_stubbed();
    crucible::test::pass("test_cntp_tcam: all PASSED\n");
    return 0;
}

#pragma GCC diagnostic pop
