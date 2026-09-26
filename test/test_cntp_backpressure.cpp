#include <crucible/cntp/Backpressure.h>
#include <crucible/cntp/BackpressureRuntime.h>
#include <fixy/Ctx.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>

namespace cntp = crucible::cntp;
namespace fe = ::foundation::effects;
namespace src = ::fixy::tags::source;

namespace {

void test_admission_helpers() {
    static_assert(::foundation::reflect::enum_name(cntp::BackpressureError::CreditOverflow)
                  == std::string_view{"CreditOverflow"});
    static_assert(cntp::admission_decision_kind_name(cntp::AdmissionDecisionKind::RejectedResource)
                  == std::string_view{"rejected_resource"});

    auto fd = cntp::admit_socket_fd(11);
    auto credit = cntp::admit_backpressure_credit(4096);
    auto zero_credit = cntp::admit_backpressure_credit(0);
    auto limit = cntp::admit_connection_limit(8);
    auto zero_limit = cntp::admit_connection_limit(0);
    auto pressure = cntp::admit_resource_pressure_ppm(900000);
    auto full_pressure = cntp::admit_resource_pressure_ppm(1000000);
    auto bad_pressure = cntp::admit_resource_pressure_ppm(1000001);
    auto resource_limit = cntp::admit_resource_limit_ppm(950000);
    auto bad_limit = cntp::admit_resource_limit_ppm(0);
    auto over_limit = cntp::admit_resource_limit_ppm(1000001);

    assert(fd.has_value());
    assert(credit.has_value());
    assert(!zero_credit.has_value());
    assert(zero_credit.error() == cntp::BackpressureError::InvalidCreditBytes);
    assert(limit.has_value());
    assert(!zero_limit.has_value());
    assert(zero_limit.error() == cntp::BackpressureError::InvalidConnectionLimit);
    assert(pressure.has_value());
    assert(full_pressure.has_value());
    assert(!bad_pressure.has_value());
    assert(bad_pressure.error() == cntp::BackpressureError::InvalidResourcePressure);
    assert(resource_limit.has_value());
    assert(!bad_limit.has_value());
    assert(bad_limit.error() == cntp::BackpressureError::InvalidResourceLimit);
    assert(!over_limit.has_value());
    assert(over_limit.error() == cntp::BackpressureError::InvalidResourceLimit);

    auto nic_pressure = cntp::mint_resource_pressure<fe::ResourceKind::NicQ>(900000);
    auto nic_limit = cntp::mint_resource_limit<fe::ResourceKind::NicQ>(800000);
    auto hbm_limit = cntp::mint_resource_limit<fe::ResourceKind::HbmBytes>(800000);
    auto bad_nic_pressure = cntp::mint_resource_pressure<fe::ResourceKind::NicQ>(1000001);
    assert(nic_pressure.has_value());
    assert(nic_limit.has_value());
    assert(hbm_limit.has_value());
    assert(!bad_nic_pressure.has_value());
    assert(bad_nic_pressure.error() == cntp::BackpressureError::InvalidResourcePressure);
    assert(nic_pressure->kind() == fe::ResourceKind::NicQ);
    assert(nic_pressure->used_ppm().value() == 900000u);
    assert(nic_limit->reject_at_or_above_ppm().value() == 800000u);
    assert(cntp::resource_pressure_exceeds(*nic_pressure, *nic_limit));
    // A pressure on one resource says nothing about a limit on another.
    assert(!cntp::resource_pressure_exceeds(*nic_pressure, *hbm_limit));

    std::printf("  test_admission_helpers:       PASSED\n");
}

void test_credit_flow_control() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg_load{fe::testing::bg()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto controller = cntp::mint_credit_flow_control<2>(init);

    auto fd0 = cntp::admit_socket_fd(20).value();
    auto fd1 = cntp::admit_socket_fd(21).value();
    auto fd2 = cntp::admit_socket_fd(22).value();
    auto initial = cntp::admit_backpressure_credit(1024).value();
    auto grant = cntp::admit_backpressure_credit(512).value();
    auto consume = cntp::admit_backpressure_credit(1536).value();

    assert(!controller.current_credit(fd0).has_value());
    assert(controller.current_credit(fd0).error() == cntp::BackpressureError::CreditFlowNotStarted);
    assert(controller.grant_credit(bg, fd0, grant).error() == cntp::BackpressureError::CreditFlowNotStarted);

    assert(controller.start_flow(bg_load, fd0, initial).has_value());
    assert(controller.grant_credit(bg, fd0, grant).has_value());
    assert(controller.current_credit(fd0).value().value() == 1536);
    assert(controller.consume_credit(bg, fd0, consume).has_value());
    auto empty = controller.current_credit(fd0);
    assert(!empty.has_value());
    assert(empty.error() == cntp::BackpressureError::CreditExhausted);
    assert(controller.consume_credit(bg, fd0, grant).error() == cntp::BackpressureError::CreditExhausted);

    assert(controller.start_flow(bg_load, fd1, initial).has_value());
    auto overflow = controller.start_flow(bg_load, fd2, initial);
    assert(!overflow.has_value());
    assert(overflow.error() == cntp::BackpressureError::TooManyCreditFlows);

    std::printf("  test_credit_flow_control:     PASSED\n");
}

void test_credit_flow_control_parallel_grants() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg_load{fe::testing::bg()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto controller = cntp::mint_credit_flow_control<1>(init);

    auto fd = cntp::admit_socket_fd(23).value();
    auto one = cntp::admit_backpressure_credit(1).value();
    assert(controller.start_flow(bg_load, fd, one).has_value());

    constexpr int kThreads = 4;
    constexpr int kGrantsPerThread = 1000;
    std::array<std::jthread, kThreads> workers{};
    for (auto& worker : workers) {
        worker = std::jthread{[&controller, bg, fd, one] {
            for (int grant_index = 0; grant_index < kGrantsPerThread; ++grant_index) {
                auto granted = controller.grant_credit(bg, fd, one);
                assert(granted.has_value());
            }
        }};
    }
    for (auto& worker : workers) {
        worker.join();
    }

    auto total = controller.current_credit(fd);
    assert(total.has_value());
    assert(total->value() == 1u + static_cast<std::uint32_t>(kThreads * kGrantsPerThread));

    auto consume_all = cntp::admit_backpressure_credit(total->value()).value();
    assert(controller.consume_credit(bg, fd, consume_all).has_value());
    assert(!controller.current_credit(fd).has_value());

    std::printf("  test_credit_flow_control_parallel_grants: PASSED\n");
}

// Two threads start the same socket at once.  The start gate serializes
// them, so the socket takes one slot and the second start updates it.
void test_credit_flow_control_parallel_start_same_fd() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg_load{fe::testing::bg()};
    auto controller = cntp::mint_credit_flow_control<1>(init);

    auto fd = cntp::admit_socket_fd(24).value();
    auto first = cntp::admit_backpressure_credit(1).value();
    auto second = cntp::admit_backpressure_credit(2).value();

    std::jthread starter_a{[&controller, bg_load, fd, first] {
        auto started = controller.start_flow(bg_load, fd, first);
        assert(started.has_value());
    }};
    std::jthread starter_b{[&controller, bg_load, fd, second] {
        auto started = controller.start_flow(bg_load, fd, second);
        assert(started.has_value());
    }};
    starter_a.join();
    starter_b.join();

    auto fd2 = cntp::admit_socket_fd(25).value();
    auto overflow = controller.start_flow(bg_load, fd2, first);
    assert(!overflow.has_value());
    assert(overflow.error() == cntp::BackpressureError::TooManyCreditFlows);
    auto current = controller.current_credit(fd);
    assert(current.has_value());
    assert(current->value() == 1u || current->value() == 2u);

    std::printf("  test_credit_flow_control_parallel_start_same_fd: PASSED\n");
}

void test_admission_controller() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto controller = cntp::mint_admission_controller<2, 2>(init);

    auto nic_limit = cntp::mint_resource_limit<fe::ResourceKind::NicQ>(900000).value();
    assert(controller.register_resource_limit(init, nic_limit).has_value());
    // A second limit on the same resource replaces the first.
    assert(controller.register_resource_limit(init, nic_limit).has_value());
    auto hbm_limit = cntp::mint_resource_limit<fe::ResourceKind::HbmBytes>(900000).value();
    auto smem_limit = cntp::mint_resource_limit<fe::ResourceKind::Smem>(900000).value();
    assert(controller.register_resource_limit(init, hbm_limit).has_value());
    assert(controller.register_resource_limit(init, smem_limit).error()
           == cntp::BackpressureError::TooManyResourceLimits);

    auto fd0 = cntp::admit_socket_fd(30).value();
    auto fd1 = cntp::admit_socket_fd(31).value();
    auto fd2 = cntp::admit_socket_fd(32).value();
    auto credit = cntp::admit_backpressure_credit(4096).value();
    cntp::ConnectionRequest const req0{.socket = fd0, .initial_credit = credit};
    cntp::ConnectionRequest const req1{.socket = fd1, .initial_credit = credit};
    cntp::ConnectionRequest const req2{.socket = fd2, .initial_credit = credit};

    std::array<cntp::ResourcePressure, 1> low{{
        cntp::mint_resource_pressure<fe::ResourceKind::NicQ>(100000).value(),
    }};
    auto accepted0 = controller.try_accept_connection(bg, req0, low);
    auto accepted1 = controller.try_accept_connection(bg, req1, low);
    assert(accepted0.has_value());
    assert(accepted1.has_value());
    assert(accepted0->value().kind == cntp::AdmissionDecisionKind::Accepted);
    assert(accepted0->value().sequence == 1);
    assert(accepted1->value().sequence == 2);
    assert(controller.live_connections() == 2);

    auto over_capacity = controller.try_accept_connection(bg, req2, low, 25);
    assert(over_capacity.has_value());
    assert(over_capacity->value().kind == cntp::AdmissionDecisionKind::RejectedBackoff);
    assert(over_capacity->value().retry_after_ms == 25);

    controller.release_connection(bg);
    assert(controller.live_connections() == 1);

    std::array<cntp::ResourcePressure, 1> high{{
        cntp::mint_resource_pressure<fe::ResourceKind::NicQ>(950000).value(),
    }};
    auto rejected = controller.try_accept_connection(bg, req2, high, 50);
    assert(rejected.has_value());
    assert(rejected->value().kind == cntp::AdmissionDecisionKind::RejectedResource);
    assert(rejected->value().limiting_resource == fe::ResourceKind::NicQ);
    assert(rejected->value().observed_ppm.value() == 950000);
    assert(rejected->value().threshold_ppm.value() == 900000);
    assert(controller.live_connections() == 1);

    controller.release_connection(bg);
    controller.release_connection(bg);
    assert(controller.live_connections() == 0);

    std::printf("  test_admission_controller:    PASSED\n");
}

}  // namespace

int main() {
    static_assert(sizeof(cntp::PositiveBackpressureBytes) == sizeof(std::uint32_t));
    static_assert(sizeof(cntp::PositiveConnectionLimit) == sizeof(std::uint16_t));
    static_assert(sizeof(cntp::DeclaredAdmissionDecision) == sizeof(cntp::AdmissionDecision));
    static_assert(std::is_trivially_copy_constructible_v<cntp::ConnectionRequest>
                  && std::is_trivially_destructible_v<cntp::ConnectionRequest>);
    static_assert(std::is_trivially_copy_constructible_v<cntp::ResourcePressure>
                  && std::is_trivially_destructible_v<cntp::ResourcePressure>);
    static_assert(std::is_trivially_copy_constructible_v<cntp::AdmissionDecision>
                  && std::is_trivially_destructible_v<cntp::AdmissionDecision>);
    static_assert(std::same_as<cntp::DeclaredAdmissionDecision::tag_type, src::AdmissionDecision>);
    // The kind of a pressure or a limit is set only by its mint.
    static_assert(!std::is_constructible_v<cntp::ResourcePressure, fe::ResourceKind, cntp::ResourcePressurePpm>);
    static_assert(!std::is_constructible_v<cntp::ResourceLimit, fe::ResourceKind, cntp::ResourceLimitPpm>);
    static_assert(cntp::CtxFitsBackpressureMint<::fixy::ColdInitCtx>);
    static_assert(!cntp::CtxFitsBackpressureMint<::fixy::BgDrainCtx>);
    static_assert(cntp::CtxFitsBackpressureRuntime<::fixy::BgDrainCtx>);
    static_assert(cntp::CtxFitsBackpressureRuntime<::fixy::TestRunnerCtx>);
    static_assert(!cntp::CtxFitsBackpressureRuntime<::fixy::HotFgCtx>);
    static_assert(cntp::CtxFitsBackpressureStart<::fixy::BgLoadCtx>);
    static_assert(!cntp::CtxFitsBackpressureStart<::fixy::BgDrainCtx>);

    std::printf("test_cntp_backpressure:\n");
    test_admission_helpers();
    test_credit_flow_control();
    test_credit_flow_control_parallel_grants();
    test_credit_flow_control_parallel_start_same_fd();
    test_admission_controller();
    std::printf("test_cntp_backpressure: all PASSED\n");
    return 0;
}
