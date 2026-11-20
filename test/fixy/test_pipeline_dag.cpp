// The diamond graph, run for real: a fan-out stage gives each value to
// two stages, a fan-in stage reads the two, and each stage body runs one
// time.  Static assertions check the ports of the diamond, the row of a
// pipeline, and the net of some stage graphs.
//
// test/fixy/test_endpoint.cpp runs a fan-in stage that it builds from
// channel endpoints.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/StageNet.h>
#include <foundation/algebra/net/Net.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

namespace cc = fixy::concurrent;

namespace pipeline_dag_test {

// Each edge joins two handles, and the handles of an edge name one channel.
template <typename T>
struct FakeChannel {};

template <typename T>
struct FakeConsumer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] ::fixy::Option<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

// The count of the stage bodies that ran.
fixy::Tally dag_calls;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

static void one_to_one_body(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept { dag_calls.add(1); }

// A fan-out stage has two output ports, and a fan-in stage has two input
// ports.  Each edge names the port that it leaves and the port that it
// enters.
static void fan_out_body(FakeConsumer<int>&&, FakeProducer<int>&&, FakeProducer<int>&&) noexcept { dag_calls.add(1); }
static void fan_in_body(FakeConsumer<int>&&, FakeConsumer<int>&&, FakeProducer<int>&&) noexcept { dag_calls.add(1); }

using PlainStage = cc::Stage<&one_to_one_body, fixy::HotFgCtx>;
using FanOutStage = cc::MpmcStage<&fan_out_body, fixy::HotFgCtx>;
using FanInStage = cc::MpmcStage<&fan_in_body, fixy::HotFgCtx>;

static_assert(cc::stage_input_count_v<FanOutStage> == 1 && cc::stage_output_count_v<FanOutStage> == 2);
static_assert(cc::stage_input_count_v<FanInStage> == 2 && cc::stage_output_count_v<FanInStage> == 1);

// The diamond.  Each port of the fan-out and the fan-in has one edge.  An
// edge from port 0 takes the port of the first edge, and port 2 is a port
// that the fan-out does not have.
template <std::size_t SecondPort>
using Diamond = cc::StageGraph<cc::StagePack<FanOutStage, PlainStage, PlainStage, FanInStage>,
                               cc::EdgePack<cc::StageEdge<0, 1, 0, 0>, cc::StageEdge<0, 2, SecondPort, 0>,
                                            cc::StageEdge<1, 3, 0, 0>, cc::StageEdge<2, 3, 0, 1>>>;

static_assert(cc::StageGraphWellFormed<Diamond<1>>);
static_assert(!cc::StageGraphWellFormed<Diamond<0>>);
static_assert(!cc::StageGraphWellFormed<Diamond<2>>);

// The net of a stage graph (fixy/concurrent/StageNet.h).  The diamond has
// four stages, an input end and an output end, and four edges and two end
// wires.  Its edges run forward, so its dataflow and its vicious circles
// pass.  The fan-out and the fan-in close a cycle with no direction, so it is
// no forest.
namespace net = ::foundation::algebra::net;

inline constexpr auto diamond_net = cc::net_of_stage_graph<Diamond<1>>();
inline constexpr net::NetVerdict diamond_dataflow = net::check_dataflow(diamond_net);
static_assert(diamond_dataflow.is_admitted(), diamond_dataflow);
static_assert(net::check_wiring(diamond_net).is_admitted() && net::check_vicious(diamond_net).is_admitted());
static_assert(net::find_forest_fault(diamond_net).code == net::NetFaultCode::ForestCycle);
static_assert(net::determinism_of(diamond_net) == net::NetDeterminism::Deterministic);
static_assert(net::check_canonical_form(diamond_net).is_admitted());

// The diamond with its two middle stages at swapped places in the stage pack
// is the same net.  A diamond whose upper path enters the second input of the
// fan-in is another net, although the gate and each check admit the two.
using MirroredDiamond = cc::StageGraph<cc::StagePack<FanOutStage, PlainStage, PlainStage, FanInStage>,
                                       cc::EdgePack<cc::StageEdge<0, 2, 0, 0>, cc::StageEdge<0, 1, 1, 0>,
                                                    cc::StageEdge<2, 3, 0, 0>, cc::StageEdge<1, 3, 0, 1>>>;
using CrossedDiamond = cc::StageGraph<cc::StagePack<FanOutStage, PlainStage, PlainStage, FanInStage>,
                                      cc::EdgePack<cc::StageEdge<0, 1, 0, 0>, cc::StageEdge<0, 2, 1, 0>,
                                                   cc::StageEdge<1, 3, 0, 1>, cc::StageEdge<2, 3, 0, 0>>>;
static_assert(net::is_same_net(diamond_net, cc::net_of_stage_graph<MirroredDiamond>()));
static_assert(!net::is_same_net(diamond_net, cc::net_of_stage_graph<CrossedDiamond>()));
static_assert(net::check_dataflow(cc::net_of_stage_graph<CrossedDiamond>()).is_admitted());

// A chain is a forest, and a chain of three is another net than a chain of
// two.  One stage is a graph too: its input and its output are the ends.
using ChainOfTwo = cc::StageGraph<cc::StagePack<PlainStage, PlainStage>, cc::EdgePack<cc::StageEdge<0, 1>>>;
using ChainOfThree = cc::StageGraph<cc::StagePack<PlainStage, PlainStage, PlainStage>,
                                    cc::EdgePack<cc::StageEdge<0, 1>, cc::StageEdge<1, 2>>>;
using OneStage = cc::StageGraph<cc::StagePack<PlainStage>, cc::EdgePack<>>;
inline constexpr auto chain_net = cc::net_of_stage_graph<ChainOfThree>();
static_assert(net::check_forest(chain_net).is_admitted() && net::check_dataflow(chain_net).is_admitted());
static_assert(net::find_canonical_form(chain_net).hash
              != net::find_canonical_form(cc::net_of_stage_graph<ChainOfTwo>()).hash);
static_assert(cc::StageGraphWellFormed<OneStage>);
static_assert(net::check_forest(cc::net_of_stage_graph<OneStage>()).is_admitted());

// The marked system of a stage graph (fixy/concurrent/StageNet.h).  The
// diamond has four stages, a source and a sink, and four edges, an input end
// and an output end.  The fake handles state no capacity, so each channel is
// a rendezvous.  The upper path of the diamond enters input 0 of the fan-in,
// and the fan-out sends on it first, so the system is live.
inline constexpr net::ProcessSystem<6, 6, 12> diamond_system = cc::marked_system_of_stage_graph<Diamond<1>>();
static_assert(diamond_system.process_names[0] == net::NetName{"stage 0"});
static_assert(diamond_system.process_names[3] == net::NetName{"stage 3"});
static_assert(diamond_system.process_names[4] == net::NetName{"source"});
static_assert(diamond_system.process_names[5] == net::NetName{"sink"});
static_assert(diamond_system.channels[1].name == net::NetName{"edge 1"});
static_assert(diamond_system.channels[4].name == net::NetName{"input 0"});
static_assert(diamond_system.channels[5].name == net::NetName{"output 0"});
static_assert(diamond_system.channels[0].capacity == 0 && diamond_system.channels[5].capacity == 0);
// The fan-out receives the input end, and then it sends on its two edges.
static_assert(diamond_system.actions[0].channel == net::ChannelId{4}
              && diamond_system.actions[0].move == net::Move::Receive);
static_assert(diamond_system.actions[1].channel == net::ChannelId{0}
              && diamond_system.actions[1].move == net::Move::Send);
static_assert(diamond_system.actions[2].channel == net::ChannelId{1}
              && diamond_system.actions[2].move == net::Move::Send);
// The source sends the input end, and the sink receives the output end.
static_assert(diamond_system.actions[10].process == net::ProcessId{4}
              && diamond_system.actions[10].channel == net::ChannelId{4});
static_assert(diamond_system.actions[11].process == net::ProcessId{5}
              && diamond_system.actions[11].move == net::Move::Receive);
inline constexpr net::NetVerdict diamond_liveness = net::check_liveness(diamond_system);
static_assert(diamond_liveness.is_admitted(), diamond_liveness);

// Handles that state the capacity of their channel: a queue of four values.
template <typename T>
struct SlottedConsumer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 4;
    [[nodiscard]] ::fixy::Option<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct SlottedProducer {
    using channel_type = FakeChannel<T>;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 4;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

static void slotted_body(SlottedConsumer<int>&&, SlottedProducer<int>&&) noexcept { dag_calls.add(1); }

using SlottedStage = cc::Stage<&slotted_body, fixy::HotFgCtx>;
using SlottedChain = cc::StageGraph<cc::StagePack<SlottedStage, SlottedStage>, cc::EdgePack<cc::StageEdge<0, 1>>>;

static_assert(cc::StatesHandleCapacity<SlottedProducer<int>> && cc::StatesHandleCapacity<SlottedConsumer<int>>);
static_assert(!cc::StatesHandleCapacity<FakeProducer<int>>);

// Each channel of the chain takes the capacity of its handle: the edge and
// the output end from the producer, and the input end from the consumer.
inline constexpr net::ProcessSystem<4, 3, 6> slotted_system = cc::marked_system_of_stage_graph<SlottedChain>();
static_assert(slotted_system.channels[0].capacity == 4 && slotted_system.channels[1].capacity == 4
              && slotted_system.channels[2].capacity == 4);
static_assert(net::check_liveness(slotted_system).is_admitted());

static void slotted_fan_out_body(SlottedConsumer<int>&&, SlottedProducer<int>&&, SlottedProducer<int>&&) noexcept {
    dag_calls.add(1);
}
static void slotted_fan_in_body(SlottedConsumer<int>&&, SlottedConsumer<int>&&, SlottedProducer<int>&&) noexcept {
    dag_calls.add(1);
}

using SlottedFanOutStage = cc::MpmcStage<&slotted_fan_out_body, fixy::HotFgCtx>;
using SlottedFanInStage = cc::MpmcStage<&slotted_fan_in_body, fixy::HotFgCtx>;

// A fan-in that receives its inputs in another order than the fan-out sends
// them.  The fan-out sends to input 1 of the fan-in first.  The fan-in
// receives input 0 first, which comes through the middle stage from the
// second send of the fan-out.  Each edge runs forward, and the gate holds.  On
// rendezvous channels each send waits for its receive, so the system holds a
// cycle of waits with no token.  A queue of one value or more on each channel
// admits it, because then each wait goes forward in the order of the stages.
template <class FanOut, class Middle, class FanIn>
using CrossedFanIn =
    cc::StageGraph<cc::StagePack<FanOut, Middle, FanIn>,
                   cc::EdgePack<cc::StageEdge<0, 2, 0, 1>, cc::StageEdge<0, 1, 1, 0>, cc::StageEdge<1, 2, 0, 0>>>;
static_assert(cc::StageGraphWellFormed<CrossedFanIn<FanOutStage, PlainStage, FanInStage>>);
static_assert(net::check_liveness(cc::marked_system_of_stage_graph<CrossedFanIn<FanOutStage, PlainStage, FanInStage>>())
                  .says("liveness: a cycle of waits holds no token: 'edge 0' step 0 from stage 0 to stage 2 <- 'edge "
                        "2' step 0 from stage 1 to stage 2 <- 'edge 1' step 0 from stage 0 to stage 1.  No action of "
                        "the cycle can start."));
static_assert(net::check_liveness(
                  cc::marked_system_of_stage_graph<CrossedFanIn<SlottedFanOutStage, SlottedStage, SlottedFanInStage>>())
                  .is_admitted());

// The coordinating context must admit the row of every stage, so the row
// of a pipeline is the union of the stage rows, and no effect more.
namespace eff = ::foundation::effects;
using BgStage = cc::Stage<&one_to_one_body, fixy::BgDrainCtx>;
using InitStage = cc::Stage<&one_to_one_body, fixy::ColdInitCtx>;
using BgAndInitRow = eff::Row<eff::Effect::Bg, eff::Effect::Alloc, eff::Effect::Init, eff::Effect::IO>;
using MixedRow = cc::pipeline_row_union_t<BgStage, PlainStage, InitStage>;

static_assert(std::is_same_v<cc::pipeline_row_union_t<PlainStage, PlainStage>, eff::Row<>>);
static_assert(eff::Subrow<MixedRow, BgAndInitRow> && eff::Subrow<BgAndInitRow, MixedRow>);

static void test_diamond_dag_runtime() {
    fixy::HotFgCtx ctx = ::foundation::effects::testing::foreground();

    auto fan_out =
        cc::mint_mpmc_stage<&fan_out_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{}, FakeProducer<int>{});
    auto left = cc::mint_stage<&one_to_one_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto right = cc::mint_stage<&one_to_one_body>(ctx, FakeConsumer<int>{}, FakeProducer<int>{});
    auto fan_in = cc::mint_mpmc_stage<&fan_in_body>(ctx, FakeConsumer<int>{}, FakeConsumer<int>{}, FakeProducer<int>{});

    using Cycle = cc::StageGraph<cc::StagePack<PlainStage, PlainStage>, cc::EdgePack<cc::StageEdge<1, 0>>>;
    static_assert(!cc::StageGraphWellFormed<Cycle>);

    const fixy::BgDrainCtx coordinator{::foundation::effects::testing::bg()};
    auto pipeline = cc::mint_pipeline_dag(coordinator, Diamond<1>{}, std::move(fan_out), std::move(left),
                                          std::move(right), std::move(fan_in));
    std::move(pipeline).run(coordinator);

    require(dag_calls.read() == 4, "the diamond did not run each of its four stages one time");
}

}  // namespace pipeline_dag_test

int main() {
    pipeline_dag_test::test_diamond_dag_runtime();
    ::fixy::check::pass(kTest, "test_pipeline_dag: ALL PASSED\n");
    return EXIT_SUCCESS;
}
