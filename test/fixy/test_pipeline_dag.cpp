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

#include "stage_loop_fakes.h"

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

// A loop of fake stages in the shape of the loop of the Vigil
// (stage_loop_fakes.h, THE LOOP of fixy/concurrent/StageNet.h).  The channels
// of the flush arm stand in the order of their first ends: the ring, the
// count, and the three edges between the stages.  Each channel takes the
// capacity that the types of its ends state.  The lock adds no action, so
// the system is the system of the pipeline with the loop of the Vigil in
// foundation/algebra/net/Priority.cpp, with the same order of the actions.
namespace loop = ::stage_loop_fakes;

// True when the action is a move of the process on the channel.
[[nodiscard]] consteval bool is_action_(net::Action const& action, net::ProcessId process, net::ChannelId channel,
                                        net::Move move) noexcept {
    return action.process == process && action.channel == channel && action.move == move;
}

inline constexpr net::ProcessSystem<5, 5, 10> flush_system = cc::marked_system_of_loop<loop::FlushArm>();
static_assert(flush_system.process_names[0] == net::NetName{"foreground"});
static_assert(flush_system.process_names[1] == net::NetName{"drain"});
static_assert(flush_system.process_names[4] == net::NetName{"publish"});
static_assert(flush_system.channels[0].name == net::NetName{"channel 0"});
static_assert(flush_system.channels[4].name == net::NetName{"channel 4"});
static_assert(flush_system.channels[0].capacity == 1024 && flush_system.channels[1].capacity == 0xFFFFFFFFu);
static_assert(flush_system.channels[2].capacity == 8 && flush_system.channels[3].capacity == 8
              && flush_system.channels[4].capacity == 8);
static_assert(is_action_(flush_system.actions[0], net::ProcessId{0}, net::ChannelId{0}, net::Move::Send));
static_assert(is_action_(flush_system.actions[1], net::ProcessId{0}, net::ChannelId{1}, net::Move::Receive));
static_assert(is_action_(flush_system.actions[2], net::ProcessId{1}, net::ChannelId{0}, net::Move::Receive));
static_assert(is_action_(flush_system.actions[3], net::ProcessId{1}, net::ChannelId{2}, net::Move::Send));
static_assert(is_action_(flush_system.actions[4], net::ProcessId{2}, net::ChannelId{2}, net::Move::Receive));
static_assert(is_action_(flush_system.actions[5], net::ProcessId{2}, net::ChannelId{3}, net::Move::Send));
static_assert(is_action_(flush_system.actions[6], net::ProcessId{3}, net::ChannelId{3}, net::Move::Receive));
static_assert(is_action_(flush_system.actions[7], net::ProcessId{3}, net::ChannelId{4}, net::Move::Send));
static_assert(is_action_(flush_system.actions[8], net::ProcessId{4}, net::ChannelId{4}, net::Move::Receive));
static_assert(is_action_(flush_system.actions[9], net::ProcessId{4}, net::ChannelId{1}, net::Move::Send));

// The door admits the two arms, and each verdict is the admitted verdict.
[[nodiscard]] consteval bool admits_the_two_arms_() noexcept {
    cc::ensure_loop_live<loop::FlushArm>();
    cc::ensure_loop_live<loop::JobArm>();
    return true;
}
static_assert(admits_the_two_arms_());
static_assert(cc::check_loop<loop::FlushArm>().is_admitted() && cc::check_loop<loop::JobArm>().is_admitted());

// The arm in which the last stage serves the job after its receive.  The
// foreground posts the job after its record, so each wait goes forward.
using PublishServesLate =
    cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>, cc::ReceivesOn<loop::Mailbox>,
                    cc::SendsOn<loop::JobDone>, cc::ReadsLock<loop::Holders>, cc::SendsOn<loop::Count>>;
using JobAfterReceive =
    cc::StageLoop<loop::ForegroundJob, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, PublishServesLate>;
static_assert(cc::check_loop<JobAfterReceive>().is_admitted());

// A foreground that posts the job before its record, with that arm of the
// last stage: the done mark waits on the receive of the stage, and the
// receive waits on the record.  A stage that polls its mailbox serves the job
// first, so a loop of that foreground takes the job arm.
using JobBeforeRecord =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Mailbox>, cc::ReceivesOn<loop::JobDone>,
                                  cc::SendsOn<loop::Ring>, cc::WritesLock<loop::Holders>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, PublishServesLate>;
static_assert(!cc::check_loop<JobBeforeRecord>().is_admitted());
static_assert(cc::check_loop<cc::StageLoop<
                  cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Mailbox>, cc::ReceivesOn<loop::JobDone>,
                                  cc::SendsOn<loop::Ring>, cc::WritesLock<loop::Holders>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, loop::PublishJob>>()
                  .is_admitted());

// The last stage serves another mailbox than the mailbox of the foreground.
// The mailbox of the foreground then has a send and no receive, so a period
// leaves a job in it (neg_stage_loop_wrong_mailbox).
using WrongMailbox = cc::StageLoop<
    loop::ForegroundJob, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
    cc::LoopProcess<loop::PublishName, cc::ReceivesOn<loop::OtherMailbox>, cc::SendsOn<loop::JobDone>,
                    cc::InputsOf<loop::PublishStage>, cc::ReadsLock<loop::Holders>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<WrongMailbox>().says(
    "loop: one period changes the count of the messages in 'channel 1': 1 sends and 0 receives.  Each period gives "
    "back the marking that it starts from, so each sequence of arms stays live."));

// The foreground posts a job and does not wait on its done mark.  Each period
// leaves a done mark in its cell, so the done mark of the next job waits for a
// free cell for ever (neg_stage_loop_token_left_behind).  The marked system
// refuses the same fault in its own words.
using TokenLeftBehind =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>, cc::SendsOn<loop::Mailbox>,
                                  cc::WritesLock<loop::Holders>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, loop::PublishJob>;
static_assert(cc::check_loop<TokenLeftBehind>().says(
    "loop: one period changes the count of the messages in 'channel 6': 1 sends and 0 receives.  Each period gives "
    "back the marking that it starts from, so each sequence of arms stays live."));
static_assert(net::check_liveness(cc::marked_system_of_loop<TokenLeftBehind>())
                  .says("liveness: channel 'channel 6' has 1 sends and 0 receives.  Each send has its receive."));

// The arm of the shift of a poll: the last stage serves the job after its
// count (stage_loop_fakes.h).  Each wait still goes forward.
static_assert(cc::check_loop<loop::JobLateArm>().is_admitted());

// The last stage reads no holders, so the lock has a writer and no reader
// (neg_stage_loop_lost_lock_end).
using LostLockEnd =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<LostLockEnd>().says(
    "loop: the processes that write the lock 'lock 0': 1, the processes that read it and do not write it: 0.  One "
    "process writes each lock, and one other process reads it."));

// The foreground reads its own lock, and the last stage does not.
using SelfReadLock =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>, cc::WritesLock<loop::Holders>,
                                  cc::ReadsLock<loop::Holders>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<SelfReadLock>().says(
    "loop: the processes that write the lock 'lock 0': 1, the processes that read it and do not write it: 0.  One "
    "process writes each lock, and one other process reads it."));

// The foreground waits on the count before it records: the cycle through the
// five processes holds no token (neg_stage_loop_waits_first).
using WaitsFirst = cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::ReceivesOn<loop::Count>,
                                                 cc::SendsOn<loop::Ring>, cc::WritesLock<loop::Holders>>,
                                 loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, loop::PublishFlush>;
static_assert(cc::check_loop<WaitsFirst>().says(
    "liveness: a cycle of waits holds no token: foreground receives 'channel 0' step 0 <- publish sends 'channel 0' "
    "step 0 <- publish receives 'channel 4' step 0 <- build sends 'channel 4' step 0 <- ....  No action of the cycle "
    "can start."));

// A job of the last stage that waits on the count: the count then has two
// receivers.
using JobFlushes =
    cc::StageLoop<loop::ForegroundJob, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::ReceivesOn<loop::Mailbox>, cc::ReceivesOn<loop::Count>,
                                  cc::SendsOn<loop::JobDone>, cc::InputsOf<loop::PublishStage>,
                                  cc::ReadsLock<loop::Holders>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<JobFlushes>().says("liveness: the receive side of channel 'channel 3' is held by "
                                                "'foreground' and by 'publish'.  One process holds each side of a "
                                                "channel."));

// The ends inside a bracket (THE LOCKS of fixy/concurrent/StageNet.h).  A
// second lock: the marks that the foreground writes, and that the last stage
// reads.
struct Marks {};

// The foreground writes the marks inside its bracket of the holders.  A
// bracket of a writer waits on nothing, so the door admits it.
using NestedBracket =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>,
                                  cc::WritesLock<loop::Holders, cc::WritesLock<Marks>>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>, cc::ReadsLock<loop::Holders>,
                                  cc::ReadsLock<Marks>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<NestedBracket>().is_admitted());
// The ends inside a bracket stand after the end of the lock, so the system is
// the system of the flush arm.
static_assert(is_action_(cc::marked_system_of_loop<NestedBracket>().actions[1], net::ProcessId{0}, net::ChannelId{1},
                         net::Move::Receive));

// The foreground waits on the count inside its bracket.  The last stage reads
// the holders before it sends the count, and the read waits while the
// bracket is open: each of the two waits on the other
// (neg_stage_loop_writer_waits_in_bracket).
using WriterWaitsInBracket =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>,
                                  cc::WritesLock<loop::Holders, cc::ReceivesOn<loop::Count>>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess, loop::PublishFlush>;
static_assert(cc::check_loop<WriterWaitsInBracket>().says(
    "loop: 'foreground' waits on 'channel 1' before it leaves its bracket of 'lock 0'.  A process that writes or "
    "reads a lock waits on nothing before it leaves the bracket."));
// The marked graph alone admits that loop, because a lock adds no action.
static_assert(net::check_liveness(cc::marked_system_of_loop<WriterWaitsInBracket>()).is_admitted());

// The last stage receives its input inside its read of the holders.  A read
// that meets a write runs again, and the receive inside it runs again with it
// (neg_stage_loop_reader_waits_in_bracket).
using ReaderWaitsInBracket =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::ReadsLock<loop::Holders, cc::InputsOf<loop::PublishStage>>,
                                  cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<ReaderWaitsInBracket>().says(
    "loop: 'publish' waits on 'channel 4' before it leaves its bracket of 'lock 0'.  A process that writes or reads "
    "a lock waits on nothing before it leaves the bracket."));

// The foreground reads the marks inside its bracket of the holders, and the
// last stage writes the marks.  A read of a lock waits while its writer is in
// a bracket, so it is a wait inside the bracket.
using LockReadInBracket =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>,
                                  cc::WritesLock<loop::Holders, cc::ReadsLock<Marks>>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>, cc::ReadsLock<loop::Holders>,
                                  cc::WritesLock<Marks>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<LockReadInBracket>().says(
    "loop: 'foreground' waits on 'lock 1' before it leaves its bracket of 'lock 0'.  A process that writes or reads "
    "a lock waits on nothing before it leaves the bracket."));

// The last stage writes the marks inside its read of the holders, and the
// foreground reads the marks.  A read of the holders that meets a write runs
// again, and then the write of the marks runs two times
// (neg_stage_loop_writer_inside_read).  The same write after the read is
// admitted.
using WriteInRead =
    cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>, cc::WritesLock<loop::Holders>,
                                  cc::ReadsLock<Marks>, cc::ReceivesOn<loop::Count>>,
                  loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>,
                                  cc::ReadsLock<loop::Holders, cc::WritesLock<Marks>>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<WriteInRead>().says(
    "loop: 'publish' writes 'lock 1' inside its read of 'lock 0'.  A read of a sequence lock can run again, and then "
    "each write inside it runs two times."));
static_assert(
    cc::check_loop<
        cc::StageLoop<cc::LoopProcess<loop::ForegroundName, cc::SendsOn<loop::Ring>, cc::WritesLock<loop::Holders>,
                                      cc::ReadsLock<Marks>, cc::ReceivesOn<loop::Count>>,
                      loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                      cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>, cc::ReadsLock<loop::Holders>,
                                      cc::WritesLock<Marks>, cc::SendsOn<loop::Count>>>>()
        .is_admitted());

// The capacities of the ends of a channel (THE LOOP).  The build stage sends
// into edge 3 through a handle of eight values, and the last stage receives
// through a handle of four (neg_stage_loop_capacity_disagrees).
using NarrowReceive = cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                                    cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::NarrowPublishStage>,
                                                    cc::ReadsLock<loop::Holders>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<NarrowReceive>().says(
    "loop: the ends of 'channel 4' state two capacities: 8 and 4.  Each end that states the capacity of a channel "
    "states the same capacity."));
// An end whose type states no capacity agrees with each capacity, so edge 3
// takes the capacity of the handle of the build stage.
using UnstatedReceive =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess, loop::BuildProcess,
                  cc::LoopProcess<loop::PublishName, cc::ReceivesOn<loop::EdgeChannel<3>>, cc::ReadsLock<loop::Holders>,
                                  cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<UnstatedReceive>().is_admitted());
static_assert(cc::marked_system_of_loop<UnstatedReceive>().channels[4].capacity == 8);
// A stage edge takes the capacity of the input handle for its receive.
using NarrowReceiveOfGraph = cc::StageLoop<loop::ForegroundFlush, loop::DrainEdges, loop::DetectEdges, loop::BuildEdges,
                                           cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::NarrowPublishStage>,
                                                           cc::ReadsLock<loop::Holders>, cc::SendsOn<loop::Count>>>;
static_assert(cc::check_loop<NarrowReceiveOfGraph>().says(
    "loop: the ends of 'channel 4' state two capacities: 8 and 4.  Each end that states the capacity of a channel "
    "states the same capacity."));

// The door over the arms (THE ARMS).  The three arms of the loop agree, and
// so do the flush arm through the handles and through the graph.
static_assert(cc::check_arms<loop::FlushArm, loop::JobArm, loop::JobLateArm>().is_admitted());
static_assert(cc::check_arms<loop::FlushArm, loop::FlushArmOfGraph>().is_admitted());
[[nodiscard]] consteval bool admits_the_three_arms_() noexcept {
    cc::ensure_arms_live<loop::FlushArm, loop::JobArm, loop::JobLateArm>();
    return true;
}
static_assert(admits_the_three_arms_());
// The door names the first arm that the door of a loop refuses.
static_assert(cc::check_arms<loop::FlushArm, LostLockEnd>().says(
    "arms: arm 1: loop: the processes that write the lock 'lock 0': 1, the processes that read it and do not write "
    "it: 0.  One process writes each lock, and one other process reads it."));
// Each arm alone is live, and edge 3 states eight values in the first arm
// and four in the second (neg_stage_loop_arms_disagree_on_capacity).
static_assert(cc::check_loop<loop::NarrowFlushArm>().is_admitted());
static_assert(cc::check_arms<loop::FlushArm, loop::NarrowFlushArm>().says(
    "arms: in arm 0 an end of 'channel 4' states the capacity 8, and in arm 1 an end of it states 4.  The arms agree "
    "on the capacity of each channel."));
// An arm whose edge 3 states no capacity agrees with each capacity.
using UnstatedEdgeArm =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess,
                  cc::LoopProcess<loop::BuildName, cc::InputsOf<loop::BuildStage>, cc::SendsOn<loop::EdgeChannel<3>>>,
                  cc::LoopProcess<loop::PublishName, cc::ReceivesOn<loop::EdgeChannel<3>>, cc::ReadsLock<loop::Holders>,
                                  cc::SendsOn<loop::Count>>>;
static_assert(cc::check_arms<loop::FlushArm, UnstatedEdgeArm, loop::NarrowFlushArm>().says(
    "arms: in arm 0 an end of 'channel 4' states the capacity 8, and in arm 2 an end of it states 4.  The arms agree "
    "on the capacity of each channel."));
static_assert(cc::check_arms<loop::FlushArm, UnstatedEdgeArm>().is_admitted());
// Each arm alone is live, and the writer of the holders is the foreground in
// the first arm and the last stage in the second
// (neg_stage_loop_arms_disagree_on_writer).
static_assert(cc::check_loop<loop::SwappedLockArm>().is_admitted());
static_assert(cc::check_arms<loop::FlushArm, loop::SwappedLockArm>().says(
    "arms: in arm 0 'foreground' writes 'lock 0', and in arm 1 'publish' writes it.  The arms agree on the writer and "
    "the reader of each lock."));
// The build stage sends the count in the second arm.
using BuildCounts =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess, loop::DetectProcess,
                  cc::LoopProcess<loop::BuildName, cc::InputsOf<loop::BuildStage>, cc::OutputsOf<loop::BuildStage>,
                                  cc::SendsOn<loop::Count>>,
                  cc::LoopProcess<loop::PublishName, cc::InputsOf<loop::PublishStage>, cc::ReadsLock<loop::Holders>>>;
static_assert(cc::check_loop<BuildCounts>().is_admitted());
static_assert(cc::check_arms<loop::FlushArm, BuildCounts>().says(
    "arms: in arm 0 'publish' sends into 'channel 1', and in arm 1 'build' sends into it.  The arms agree on each "
    "channel: one process sends into it, and one process receives from it."));
// Two processes of one arm with one name.
using DrainTwice =
    cc::StageLoop<loop::ForegroundFlush, loop::DrainProcess,
                  cc::LoopProcess<loop::DrainName, cc::InputsOf<loop::DetectStage>, cc::OutputsOf<loop::DetectStage>>,
                  loop::BuildProcess, loop::PublishFlush>;
static_assert(cc::check_loop<DrainTwice>().is_admitted());
static_assert(cc::check_arms<DrainTwice>().says(
    "arms: arm 0 holds two processes with the name 'drain'.  The arms compare their processes by the type of the "
    "name, so each process of an arm has a name of its own."));

// The graph of a minted pipeline (pipeline_graph_t): the chain of its stages,
// or the graph of a PipelineDag.
static_assert(std::is_same_v<
              loop::FakeGraph,
              cc::StageGraph<cc::StagePack<loop::DrainStage, loop::DetectStage, loop::BuildStage, loop::PublishStage>,
                             cc::EdgePack<cc::StageEdge<0, 1>, cc::StageEdge<1, 2>, cc::StageEdge<2, 3>>>>);
static_assert(cc::StageGraphWellFormed<loop::FakeGraph>);
static_assert(std::is_same_v<cc::pipeline_graph_t<cc::PipelineDag<Diamond<1>>>, Diamond<1>>);

// True when two systems hold the same names, capacities and actions.  The
// two arguments can swap with no change of the result.
template <std::size_t Processes, std::size_t Channels, std::size_t Actions>
[[nodiscard]] consteval bool is_same_system_(net::ProcessSystem<Processes, Channels, Actions> const& left,
                                             net::ProcessSystem<Processes, Channels, Actions> const& right) noexcept {
    for (std::size_t process = 0; process < Processes; ++process) {
        if (!(left.process_names[process] == right.process_names[process])) return false;
    }
    for (std::size_t channel = 0; channel < Channels; ++channel) {
        if (!(left.channels[channel].name == right.channels[channel].name)
            || left.channels[channel].capacity != right.channels[channel].capacity)
            return false;
    }
    for (std::size_t action = 0; action < Actions; ++action) {
        net::Action const& other = right.actions[action];
        if (!is_action_(left.actions[action], other.process, other.channel, other.move)) return false;
    }
    return true;
}

// The stages read through the edges of the graph give the system of the
// stages read through their handles.
static_assert(is_same_system_(cc::marked_system_of_loop<loop::FlushArmOfGraph>(), flush_system));
static_assert(cc::check_loop<loop::FlushArmOfGraph>().is_admitted());

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
