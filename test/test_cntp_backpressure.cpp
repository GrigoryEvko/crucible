#include <crucible/cntp/Backpressure.h>
#include <crucible/cntp/BackpressureRuntime.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include <foundation/reflect/EnumName.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace cntp = crucible::cntp;
namespace fe = ::foundation::effects;
namespace src = ::fixy::tags::source;

namespace {

// The value of a call that succeeds in this test.
template <class Value>
Value take(::fixy::Result<Value, cntp::BackpressureError> result) {
    return std::move(result).expect("the call succeeds in this test");
}

// The error of a call that fails in this test.  A call that succeeds ends
// the process.
template <class Value>
cntp::BackpressureError error_of(::fixy::Result<Value, cntp::BackpressureError> result) {
    return std::move(result).err().expect("the call fails in this test");
}

void test_admission_helpers() {
    static_assert(::foundation::reflect::enum_name(cntp::BackpressureError::CreditOverflow)
                  == std::string_view{"CreditOverflow"});
    static_assert(cntp::admission_decision_kind_name(cntp::AdmissionDecisionKind::RejectedResource)
                  == "rejected_resource");

    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_backpressure_credit(4096).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(cntp::admit_backpressure_credit(0)) == cntp::BackpressureError::InvalidCreditBytes);
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_connection_limit(8).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(cntp::admit_connection_limit(0)) == cntp::BackpressureError::InvalidConnectionLimit);
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_resource_pressure_ppm(900000).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_resource_pressure_ppm(1000000).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::admit_resource_pressure_ppm(1000001))
                                    == cntp::BackpressureError::InvalidResourcePressure);
    CRUCIBLE_TEST_ASSERT(kTest, cntp::admit_resource_limit_ppm(950000).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(cntp::admit_resource_limit_ppm(0)) == cntp::BackpressureError::InvalidResourceLimit);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::admit_resource_limit_ppm(1000001))
                                    == cntp::BackpressureError::InvalidResourceLimit);

    cntp::ResourcePressure const nic_pressure = take(cntp::mint_resource_pressure<fe::ResourceKind::NicQ>(900000));
    cntp::ResourceLimit const nic_limit = take(cntp::mint_resource_limit<fe::ResourceKind::NicQ>(800000));
    cntp::ResourceLimit const hbm_limit = take(cntp::mint_resource_limit<fe::ResourceKind::HbmBytes>(800000));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(cntp::mint_resource_pressure<fe::ResourceKind::NicQ>(1000001))
                                    == cntp::BackpressureError::InvalidResourcePressure);
    CRUCIBLE_TEST_ASSERT(kTest, nic_pressure.kind() == fe::ResourceKind::NicQ);
    CRUCIBLE_TEST_ASSERT(kTest, nic_pressure.used_ppm().value() == 900000u);
    CRUCIBLE_TEST_ASSERT(kTest, nic_limit.reject_at_or_above_ppm().value() == 800000u);
    CRUCIBLE_TEST_ASSERT(kTest, cntp::resource_pressure_exceeds(nic_pressure, nic_limit));
    // A pressure on one resource says nothing about a limit on another.
    CRUCIBLE_TEST_ASSERT(kTest, !cntp::resource_pressure_exceeds(nic_pressure, hbm_limit));

    ::fixy::check::pass(kTest, "  test_admission_helpers:       PASSED\n");
}

void test_credit_flow_control() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg_load{fe::testing::bg()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto controller = cntp::mint_credit_flow_control<2>(init);

    // The flows key their sockets by identity, and no call of this test
    // reaches the kernel.
    cntp::SocketIdentity const fd0{std::uint32_t{20}};
    cntp::SocketIdentity const fd1{std::uint32_t{21}};
    cntp::SocketIdentity const fd2{std::uint32_t{22}};
    cntp::PositiveBackpressureBytes const initial = take(cntp::admit_backpressure_credit(1024));
    cntp::PositiveBackpressureBytes const grant = take(cntp::admit_backpressure_credit(512));
    cntp::PositiveBackpressureBytes const consume = take(cntp::admit_backpressure_credit(1536));

    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(controller.current_credit(fd0)) == cntp::BackpressureError::CreditFlowNotStarted);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(controller.grant_credit(bg, fd0, grant))
                                    == cntp::BackpressureError::CreditFlowNotStarted);

    CRUCIBLE_TEST_ASSERT(kTest, controller.start_flow(bg_load, fd0, initial).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, controller.grant_credit(bg, fd0, grant).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, take(controller.current_credit(fd0)).value() == 1536);
    CRUCIBLE_TEST_ASSERT(kTest, controller.consume_credit(bg, fd0, consume).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(controller.current_credit(fd0)) == cntp::BackpressureError::CreditExhausted);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(controller.consume_credit(bg, fd0, grant))
                                    == cntp::BackpressureError::CreditExhausted);

    CRUCIBLE_TEST_ASSERT(kTest, controller.start_flow(bg_load, fd1, initial).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(controller.start_flow(bg_load, fd2, initial))
                                    == cntp::BackpressureError::TooManyCreditFlows);

    ::fixy::check::pass(kTest, "  test_credit_flow_control:     PASSED\n");
}

void test_credit_flow_control_parallel_grants() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg_load{fe::testing::bg()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto controller = cntp::mint_credit_flow_control<1>(init);

    cntp::SocketIdentity const fd{std::uint32_t{23}};
    cntp::PositiveBackpressureBytes const one = take(cntp::admit_backpressure_credit(1));
    CRUCIBLE_TEST_ASSERT(kTest, controller.start_flow(bg_load, fd, one).is_ok());

    constexpr int kThreads = 4;
    constexpr int kGrantsPerThread = 1000;
    std::array<std::jthread, kThreads> workers{};
    for (auto& worker : workers) {
        worker = std::jthread{[&controller, bg, fd, one] {
            for (int grant_index = 0; grant_index < kGrantsPerThread; ++grant_index) {
                CRUCIBLE_TEST_ASSERT(kTest, controller.grant_credit(bg, fd, one).is_ok());
            }
        }};
    }
    for (auto& worker : workers) {
        worker.join();
    }

    cntp::PositiveBackpressureBytes const total = take(controller.current_credit(fd));
    CRUCIBLE_TEST_ASSERT(kTest, total.value() == 1u + static_cast<std::uint32_t>(kThreads * kGrantsPerThread));

    CRUCIBLE_TEST_ASSERT(kTest, controller.consume_credit(bg, fd, total).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, controller.current_credit(fd).is_err());

    ::fixy::check::pass(kTest, "  test_credit_flow_control_parallel_grants: PASSED\n");
}

// Two threads start the same socket at once.  The start gate serializes
// them, so the socket takes one slot and the second start updates it.
void test_credit_flow_control_parallel_start_same_fd() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgLoadCtx bg_load{fe::testing::bg()};
    auto controller = cntp::mint_credit_flow_control<1>(init);

    cntp::SocketIdentity const fd{std::uint32_t{24}};
    cntp::PositiveBackpressureBytes const first = take(cntp::admit_backpressure_credit(1));
    cntp::PositiveBackpressureBytes const second = take(cntp::admit_backpressure_credit(2));

    std::jthread starter_a{[&controller, bg_load, fd, first] {
        CRUCIBLE_TEST_ASSERT(kTest, controller.start_flow(bg_load, fd, first).is_ok());
    }};
    std::jthread starter_b{[&controller, bg_load, fd, second] {
        CRUCIBLE_TEST_ASSERT(kTest, controller.start_flow(bg_load, fd, second).is_ok());
    }};
    starter_a.join();
    starter_b.join();

    cntp::SocketIdentity const fd2{std::uint32_t{25}};
    CRUCIBLE_TEST_ASSERT(kTest, error_of(controller.start_flow(bg_load, fd2, first))
                                    == cntp::BackpressureError::TooManyCreditFlows);
    std::uint32_t const current = take(controller.current_credit(fd)).value();
    CRUCIBLE_TEST_ASSERT(kTest, current == 1u || current == 2u);

    ::fixy::check::pass(kTest, "  test_credit_flow_control_parallel_start_same_fd: PASSED\n");
}

void test_admission_controller() {
    ::fixy::ColdInitCtx init{fe::testing::init()};
    ::fixy::BgDrainCtx bg{fe::testing::bg()};
    auto controller = cntp::mint_admission_controller<2, 2>(init);

    cntp::ResourceLimit const nic_limit = take(cntp::mint_resource_limit<fe::ResourceKind::NicQ>(900000));
    CRUCIBLE_TEST_ASSERT(kTest, controller.register_resource_limit(init, nic_limit).is_ok());
    // A second limit on the same resource replaces the first.
    CRUCIBLE_TEST_ASSERT(kTest, controller.register_resource_limit(init, nic_limit).is_ok());
    cntp::ResourceLimit const hbm_limit = take(cntp::mint_resource_limit<fe::ResourceKind::HbmBytes>(900000));
    cntp::ResourceLimit const smem_limit = take(cntp::mint_resource_limit<fe::ResourceKind::Smem>(900000));
    CRUCIBLE_TEST_ASSERT(kTest, controller.register_resource_limit(init, hbm_limit).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(controller.register_resource_limit(init, smem_limit))
                                    == cntp::BackpressureError::TooManyResourceLimits);

    cntp::SocketIdentity const fd0{std::uint32_t{30}};
    cntp::SocketIdentity const fd1{std::uint32_t{31}};
    cntp::SocketIdentity const fd2{std::uint32_t{32}};
    cntp::PositiveBackpressureBytes const credit = take(cntp::admit_backpressure_credit(4096));
    cntp::ConnectionRequest const req0{.socket = fd0, .initial_credit = credit};
    cntp::ConnectionRequest const req1{.socket = fd1, .initial_credit = credit};
    cntp::ConnectionRequest const req2{.socket = fd2, .initial_credit = credit};

    ::foundation::core::FixedArray<cntp::ResourcePressure, 1> const low{
        take(cntp::mint_resource_pressure<fe::ResourceKind::NicQ>(100000))};
    cntp::DeclaredAdmissionDecision const accepted0 = take(controller.try_accept_connection(bg, req0, low.view()));
    cntp::DeclaredAdmissionDecision const accepted1 = take(controller.try_accept_connection(bg, req1, low.view()));
    CRUCIBLE_TEST_ASSERT(kTest, accepted0.value().kind == cntp::AdmissionDecisionKind::Accepted);
    CRUCIBLE_TEST_ASSERT(kTest, accepted0.value().sequence == 1);
    CRUCIBLE_TEST_ASSERT(kTest, accepted1.value().sequence == 2);
    CRUCIBLE_TEST_ASSERT(kTest, controller.live_connections() == 2);

    cntp::DeclaredAdmissionDecision const over_capacity =
        take(controller.try_accept_connection(bg, req2, low.view(), 25));
    CRUCIBLE_TEST_ASSERT(kTest, over_capacity.value().kind == cntp::AdmissionDecisionKind::RejectedBackoff);
    CRUCIBLE_TEST_ASSERT(kTest, over_capacity.value().retry_after_ms == 25);

    controller.release_connection(bg);
    CRUCIBLE_TEST_ASSERT(kTest, controller.live_connections() == 1);

    ::foundation::core::FixedArray<cntp::ResourcePressure, 1> const high{
        take(cntp::mint_resource_pressure<fe::ResourceKind::NicQ>(950000))};
    cntp::DeclaredAdmissionDecision const rejected = take(controller.try_accept_connection(bg, req2, high.view(), 50));
    CRUCIBLE_TEST_ASSERT(kTest, rejected.value().kind == cntp::AdmissionDecisionKind::RejectedResource);
    CRUCIBLE_TEST_ASSERT(kTest, rejected.value().limiting_resource == fe::ResourceKind::NicQ);
    CRUCIBLE_TEST_ASSERT(kTest, rejected.value().observed_ppm.value() == 950000);
    CRUCIBLE_TEST_ASSERT(kTest, rejected.value().threshold_ppm.value() == 900000);
    CRUCIBLE_TEST_ASSERT(kTest, controller.live_connections() == 1);

    controller.release_connection(bg);
    controller.release_connection(bg);
    CRUCIBLE_TEST_ASSERT(kTest, controller.live_connections() == 0);

    ::fixy::check::pass(kTest, "  test_admission_controller:    PASSED\n");
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

    ::fixy::report(::fixy::Sink::Out, "test_cntp_backpressure:\n");
    test_admission_helpers();
    test_credit_flow_control();
    test_credit_flow_control_parallel_grants();
    test_credit_flow_control_parallel_start_same_fd();
    test_admission_controller();
    ::fixy::check::pass(kTest, "test_cntp_backpressure: all PASSED\n");
    return 0;
}
