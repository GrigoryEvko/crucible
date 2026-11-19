// Each BPF object that the build embeds is an ELF object of the BPF machine.
//
// cmake/CrucibleBpf.cmake compiles each program that CMakeLists.txt names with
// crucible_bpf_program, and it embeds the object in an embed unit of
// crucible_perf.  The test calls the function of each of the 34 embed units
// and reads the head of its bytes: the ELF magic, the 64-bit class, the
// little-endian data and the machine EM_BPF.  SenseHub v2 is not in the list,
// because no configuration of the gate compiles it.  The test loads no
// program, so it needs no capability.

#include <crucible/perf/detail/BpfLoader.h>
#include <fixy/Core.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace detail = crucible::perf::detail;

namespace {

// The head of an ELF header: e_ident (16 bytes), e_type (2 bytes) and
// e_machine (2 bytes).
inline constexpr std::size_t kElfHeadBytes = 20;

// The bytes 0 to 3 of e_ident are the magic, byte 4 is the class and byte 5
// is the data encoding.  ELFCLASS64 is 2 and ELFDATA2LSB is 1.  e_machine is
// a little-endian word at byte 18, and EM_BPF is 247.
[[nodiscard]] bool is_bpf_elf_head(::fixy::View<unsigned char const, kElfHeadBytes> head) noexcept {
    bool const has_magic = head.at(::fixy::index<0>()) == 0x7fu && head.at(::fixy::index<1>()) == 'E'
                        && head.at(::fixy::index<2>()) == 'L' && head.at(::fixy::index<3>()) == 'F';
    bool const is_elf64_lsb = head.at(::fixy::index<4>()) == 2u && head.at(::fixy::index<5>()) == 1u;
    bool const is_bpf_machine = head.at(::fixy::index<18>()) == 247u && head.at(::fixy::index<19>()) == 0u;
    return has_magic && is_elf64_lsb && is_bpf_machine;
}

// Ends the test when the bytes of the object of name are empty, or when
// they do not start with the head of an ELF object of the BPF machine.
void expect_bpf_object(::fixy::TextView name, detail::ForeignBytes bytes) noexcept {
    ::fixy::View<unsigned char const> const view = detail::view_bytes(bytes);
    CRUCIBLE_TEST_ASSERT(kTest, view.size() > 0);
    bool const is_bpf_object = view.window<kElfHeadBytes>(0).match(
        [](::fixy::View<unsigned char const, kElfHeadBytes> head) noexcept { return is_bpf_elf_head(head); },
        []() noexcept { return false; });
    CRUCIBLE_TEST_ASSERT(kTest, is_bpf_object);
    ::fixy::check::pass(kTest, "  {}: {} bytes\n", name, view.size());
}

void test_perf_programs() {
    expect_bpf_object("sense_hub", detail::sense_hub_bpf_object());
    expect_bpf_object("sched_switch", detail::sched_switch_bpf_object());
    expect_bpf_object("pmu_sample", detail::pmu_sample_bpf_object());
    expect_bpf_object("lock_contention", detail::lock_contention_bpf_object());
    expect_bpf_object("syscall_latency", detail::syscall_latency_bpf_object());
    expect_bpf_object("sched_tp_btf", detail::sched_tp_btf_bpf_object());
    expect_bpf_object("syscall_tp_btf", detail::syscall_tp_btf_bpf_object());
    ::fixy::check::pass(kTest, "  test_perf_programs: PASSED\n");
}

void test_continuous_monitoring_stubs() {
    expect_bpf_object("cntp_dp_tc_ingress_classify", detail::cntp_dp_tc_ingress_classify_bpf_object());
    expect_bpf_object("cntp_dp_tc_egress_classify", detail::cntp_dp_tc_egress_classify_bpf_object());
    expect_bpf_object("cntp_dp_pacing_budget", detail::cntp_dp_pacing_budget_bpf_object());
    expect_bpf_object("cntp_dp_queue_backlog", detail::cntp_dp_queue_backlog_bpf_object());
    expect_bpf_object("cntp_dp_fq_budget", detail::cntp_dp_fq_budget_bpf_object());
    expect_bpf_object("cntp_xdp_flow_filter", detail::cntp_xdp_flow_filter_bpf_object());
    expect_bpf_object("cntp_tc_egress_mark", detail::cntp_tc_egress_mark_bpf_object());
    expect_bpf_object("cntp_sk_msg_credit_gate", detail::cntp_sk_msg_credit_gate_bpf_object());
    expect_bpf_object("cntp_sock_ops_telemetry", detail::cntp_sock_ops_telemetry_bpf_object());
    expect_bpf_object("cntp_ecn_ce_mark_sample", detail::cntp_ecn_ce_mark_sample_bpf_object());
    expect_bpf_object("cntp_wip_fq_pacing_sample", detail::cntp_wip_fq_pacing_sample_bpf_object());
    expect_bpf_object("cntp_wip_bbr_model_sample", detail::cntp_wip_bbr_model_sample_bpf_object());
    expect_bpf_object("cntp_wip_ack_clock_sample", detail::cntp_wip_ack_clock_sample_bpf_object());
    expect_bpf_object("cntp_wip_loss_recovery_sample", detail::cntp_wip_loss_recovery_sample_bpf_object());
    expect_bpf_object("warden_socket_lifecycle", detail::warden_socket_lifecycle_bpf_object());
    expect_bpf_object("canopy_gossip_xdp_tx", detail::canopy_gossip_xdp_tx_bpf_object());
    expect_bpf_object("canopy_peer_liveness_probe", detail::canopy_peer_liveness_probe_bpf_object());
    expect_bpf_object("canopy_gossip_fanout_sample", detail::canopy_gossip_fanout_sample_bpf_object());
    expect_bpf_object("topology_xdp_rx_timestamp_probe", detail::topology_xdp_rx_timestamp_probe_bpf_object());
    expect_bpf_object("topology_napi_pressure_probe", detail::topology_napi_pressure_probe_bpf_object());
    expect_bpf_object("topology_qdisc_backlog_probe", detail::topology_qdisc_backlog_probe_bpf_object());
    expect_bpf_object("topology_lldp_rx_sample", detail::topology_lldp_rx_sample_bpf_object());
    expect_bpf_object("observe_tcp_flow_sample", detail::observe_tcp_flow_sample_bpf_object());
    expect_bpf_object("observe_packet_loss_sample", detail::observe_packet_loss_sample_bpf_object());
    expect_bpf_object("observe_ecn_sample", detail::observe_ecn_sample_bpf_object());
    expect_bpf_object("cog_nic_cog_probe", detail::cog_nic_cog_probe_bpf_object());
    expect_bpf_object("cog_offload_cap_probe", detail::cog_offload_cap_probe_bpf_object());
    ::fixy::check::pass(kTest, "  test_continuous_monitoring_stubs: PASSED\n");
}

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "test_perf_bpf_objects:\n");
    test_perf_programs();
    test_continuous_monitoring_stubs();
    ::fixy::check::pass(kTest, "test_perf_bpf_objects: all PASSED\n");
    return 0;
}
