#pragma once

// The processes of a loop of fake stages in the shape of the loop of the
// Vigil (the loop of fixy/concurrent/StageNet.h): a foreground, four stages
// and the cells around them.  The foreground sends into the ring, and the
// first stage receives from it.  The last stage sends into the count, and
// the foreground receives from it.  A job of the foreground goes into the
// mailbox, and the last stage sends the done mark.  The foreground writes the
// lock of its holders in a bracket, and the last stage reads it.
//
// test_pipeline_dag.cpp checks the loops that this header gives, and each
// fixture neg_stage_loop_* changes one end of one of them.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/concurrent/Pipeline.h>
#include <fixy/concurrent/StageNet.h>
#include <foundation/algebra/net/Net.h>

#include <cstddef>

namespace stage_loop_fakes {

namespace cc = ::fixy::concurrent;
namespace net = ::foundation::algebra::net;

// The channel of one edge of the fake pipeline.  Each edge is a type of its
// own, so each edge is a channel of its own.
template <int Edge>
struct EdgeChannel {};

// The two handles of an edge: a queue of eight values.
template <int Edge>
struct EdgeConsumer {
    using channel_type = EdgeChannel<Edge>;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 8;
    [[nodiscard]] ::fixy::Option<int> try_pop() noexcept { return {}; }
};

template <int Edge>
struct EdgeProducer {
    using channel_type = EdgeChannel<Edge>;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 8;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

// The two handles of an edge that state a queue of four values on the same
// channel as EdgeConsumer and EdgeProducer.
template <int Edge>
struct NarrowConsumer {
    using channel_type = EdgeChannel<Edge>;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 4;
    [[nodiscard]] ::fixy::Option<int> try_pop() noexcept { return {}; }
};

template <int Edge>
struct NarrowProducer {
    using channel_type = EdgeChannel<Edge>;
    static constexpr std::size_t per_call_working_set = 64;
    static constexpr std::size_t channel_capacity = 4;
    [[nodiscard]] bool try_push(int const&) noexcept { return true; }
};

// The bodies of the four stages.  Edge 0 is the go signal of the first
// stage, and edge 4 is the end mark of the last stage, so the loop reads
// neither of them.
inline void drain_body(EdgeConsumer<0>&&, EdgeProducer<1>&&) noexcept {}
inline void detect_body(EdgeConsumer<1>&&, EdgeProducer<2>&&) noexcept {}
inline void build_body(EdgeConsumer<2>&&, EdgeProducer<3>&&) noexcept {}
inline void publish_body(EdgeConsumer<3>&&, EdgeProducer<4>&&) noexcept {}

// The bodies of a build stage and a last stage whose handles of edge 3 state
// the narrow queue.
inline void narrow_build_body(EdgeConsumer<2>&&, NarrowProducer<3>&&) noexcept {}
inline void narrow_publish_body(NarrowConsumer<3>&&, EdgeProducer<4>&&) noexcept {}

using DrainStage = cc::Stage<&drain_body, ::fixy::HotFgCtx>;
using DetectStage = cc::Stage<&detect_body, ::fixy::HotFgCtx>;
using BuildStage = cc::Stage<&build_body, ::fixy::HotFgCtx>;
using PublishStage = cc::Stage<&publish_body, ::fixy::HotFgCtx>;
using NarrowBuildStage = cc::Stage<&narrow_build_body, ::fixy::HotFgCtx>;
using NarrowPublishStage = cc::Stage<&narrow_publish_body, ::fixy::HotFgCtx>;

// The cells around the stages, each with the count of the messages that it
// holds.  The count holds each bump, so its writer never waits.
struct Ring {
    static constexpr std::size_t channel_capacity = 1024;
};
struct Count {
    static constexpr std::size_t channel_capacity = 0xFFFFFFFFu;
};
struct Mailbox {
    static constexpr std::size_t channel_capacity = 1;
};
struct JobDone {
    static constexpr std::size_t channel_capacity = 1;
};

// A mailbox that no foreground sends into.
struct OtherMailbox {
    static constexpr std::size_t channel_capacity = 1;
};

// The sequence lock of the regions that the foreground holds.
struct Holders {};

// The names of the processes.
struct ForegroundName {
    static constexpr net::NetName process_name{"foreground"};
};
struct DrainName {
    static constexpr net::NetName process_name{"drain"};
};
struct DetectName {
    static constexpr net::NetName process_name{"detect"};
};
struct BuildName {
    static constexpr net::NetName process_name{"build"};
};
struct PublishName {
    static constexpr net::NetName process_name{"publish"};
};

// The three middle processes, the same in each arm.
using DrainProcess = cc::LoopProcess<DrainName, cc::ReceivesOn<Ring>, cc::OutputsOf<DrainStage>>;
using DetectProcess = cc::LoopProcess<DetectName, cc::InputsOf<DetectStage>, cc::OutputsOf<DetectStage>>;
using BuildProcess = cc::LoopProcess<BuildName, cc::InputsOf<BuildStage>, cc::OutputsOf<BuildStage>>;

// The flush arm: the foreground records, writes its holders and waits on the
// count.  The last stage receives, reads the holders and sends the count.
using ForegroundFlush =
    cc::LoopProcess<ForegroundName, cc::SendsOn<Ring>, cc::WritesLock<Holders>, cc::ReceivesOn<Count>>;
using PublishFlush =
    cc::LoopProcess<PublishName, cc::InputsOf<PublishStage>, cc::ReadsLock<Holders>, cc::SendsOn<Count>>;

// The job arm: the foreground records, posts a job, waits on its done mark,
// writes its holders and waits on the count.  The last stage serves the job
// before its receive.
using ForegroundJob = cc::LoopProcess<ForegroundName, cc::SendsOn<Ring>, cc::SendsOn<Mailbox>, cc::ReceivesOn<JobDone>,
                                      cc::WritesLock<Holders>, cc::ReceivesOn<Count>>;
using PublishJob = cc::LoopProcess<PublishName, cc::ReceivesOn<Mailbox>, cc::SendsOn<JobDone>,
                                   cc::InputsOf<PublishStage>, cc::ReadsLock<Holders>, cc::SendsOn<Count>>;

using FlushArm = cc::StageLoop<ForegroundFlush, DrainProcess, DetectProcess, BuildProcess, PublishFlush>;
using JobArm = cc::StageLoop<ForegroundJob, DrainProcess, DetectProcess, BuildProcess, PublishJob>;

// The job arm of the shift of a poll (the arms of fixy/concurrent/StageNet.h).
// The last stage polls its mailbox before it receives, so a job that comes
// after the poll waits for the next period of the stage.  The stage then
// serves the job after its count, at the end of the period of the job.
using PublishJobLate = cc::LoopProcess<PublishName, cc::InputsOf<PublishStage>, cc::ReadsLock<Holders>,
                                       cc::SendsOn<Count>, cc::ReceivesOn<Mailbox>, cc::SendsOn<JobDone>>;
using JobLateArm = cc::StageLoop<ForegroundJob, DrainProcess, DetectProcess, BuildProcess, PublishJobLate>;

// The flush arm with the narrow handles on edge 3.  The two ends of edge 3
// state the capacity 4, so the arm agrees with itself, and edge 3 holds 8
// values in FlushArm.
using NarrowFlushArm = cc::StageLoop<
    ForegroundFlush, DrainProcess, DetectProcess,
    cc::LoopProcess<BuildName, cc::InputsOf<NarrowBuildStage>, cc::OutputsOf<NarrowBuildStage>>,
    cc::LoopProcess<PublishName, cc::InputsOf<NarrowPublishStage>, cc::ReadsLock<Holders>, cc::SendsOn<Count>>>;

// The flush arm in which the last stage writes the holders, and the
// foreground reads them.  The arm lists the last stage first: the arms
// compare their processes by name, and not by place.
using SwappedLockArm =
    cc::StageLoop<cc::LoopProcess<PublishName, cc::InputsOf<PublishStage>, cc::WritesLock<Holders>, cc::SendsOn<Count>>,
                  cc::LoopProcess<ForegroundName, cc::SendsOn<Ring>, cc::ReadsLock<Holders>, cc::ReceivesOn<Count>>,
                  DrainProcess, DetectProcess, BuildProcess>;

// The fake stages as a minted pipeline names them, and the graph of that
// pipeline: the chain of the four stages.
using FakePipeline = cc::Pipeline<DrainStage, DetectStage, BuildStage, PublishStage>;
using FakeGraph = cc::pipeline_graph_t<FakePipeline>;

// The processes of the stages over the edges of the graph.  The input end of
// the first stage and the output end of the last stage are no edges.
using DrainEdges = cc::LoopProcess<DrainName, cc::ReceivesOn<Ring>, cc::StageEdges<FakeGraph, 0>>;
using DetectEdges = cc::LoopProcess<DetectName, cc::StageEdges<FakeGraph, 1>>;
using BuildEdges = cc::LoopProcess<BuildName, cc::StageEdges<FakeGraph, 2>>;
using PublishFlushEdges =
    cc::LoopProcess<PublishName, cc::StageEdges<FakeGraph, 3>, cc::ReadsLock<Holders>, cc::SendsOn<Count>>;
using FlushArmOfGraph = cc::StageLoop<ForegroundFlush, DrainEdges, DetectEdges, BuildEdges, PublishFlushEdges>;

}  // namespace stage_loop_fakes
