#include <crucible/concurrent/Pipeline.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/permissions/Permission.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace cc = crucible::concurrent;
namespace eff = crucible::effects;
namespace saf = crucible::safety;

namespace pipeline_dag_test {

template <typename T>
struct FakeConsumer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

template <typename T>
struct FakeProducer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] bool try_push(T const&) noexcept { return true; }
};

struct InATag {};
struct InBTag {};
struct InCTag {};
struct OutTag {};
struct Out2Tag {};
struct SwmrInTag {};
struct SnapTag {};

using InA = cc::PermissionedSpscChannel<int, 64, InATag>;
using InB = cc::PermissionedSpscChannel<int, 64, InBTag>;
using InC = cc::PermissionedSpscChannel<int, 64, InCTag>;
using Out = cc::PermissionedSpscChannel<int, 64, OutTag>;
using Out2 = cc::PermissionedSpscChannel<int, 64, Out2Tag>;
using SwmrIn = cc::PermissionedSpscChannel<int, 64, SwmrInTag>;
using Snapshot = cc::PermissionedSnapshot<int, SnapTag>;

std::atomic<int> fan_in_calls{0};
std::atomic<int> fan_out_calls{0};
std::atomic<int> dag_calls{0};
std::atomic<int> swmr_calls{0};

static void reset() noexcept {
    fan_in_calls.store(0, std::memory_order_relaxed);
    fan_out_calls.store(0, std::memory_order_relaxed);
    dag_calls.store(0, std::memory_order_relaxed);
    swmr_calls.store(0, std::memory_order_relaxed);
}

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::abort();
    }
}

template <typename Channel>
[[nodiscard]] auto split_spsc() noexcept {
    auto whole = saf::mint_permission_root<typename Channel::whole_tag>();
    return saf::mint_permission_split<
        typename Channel::producer_tag,
        typename Channel::consumer_tag>(std::move(whole));
}

static void fan_in_body(InA::ConsumerHandle&&,
                        InB::ConsumerHandle&&,
                        InC::ConsumerHandle&&,
                        Out::ProducerHandle&&) noexcept {
    fan_in_calls.fetch_add(1, std::memory_order_relaxed);
}

static void fan_out_body(InA::ConsumerHandle&&,
                         Out::ProducerHandle&&,
                         Out2::ProducerHandle&&) noexcept {
    fan_out_calls.fetch_add(1, std::memory_order_relaxed);
}

static void swmr_publish_body(SwmrIn::ConsumerHandle&&,
                              Snapshot::WriterHandle&& writer) noexcept {
    writer.publish(7);
    swmr_calls.fetch_add(1, std::memory_order_relaxed);
}

static void one_to_one_body(FakeConsumer<int>&&, FakeProducer<int>&&) noexcept {
    dag_calls.fetch_add(1, std::memory_order_relaxed);
}

using PlainStage = cc::Stage<&one_to_one_body, eff::HotFgCtx>;

static_assert(cc::SwmrPublishStageBody<&swmr_publish_body>);
static_assert(!saf::extract::PipelineStage<&swmr_publish_body>);

static void test_mpmc_stage_from_endpoints_fan_in() {
    reset();
    eff::HotFgCtx ctx{};

    InA in_a;
    InB in_b;
    InC in_c;
    Out out;

    auto [a_prod_perm, a_cons_perm] = split_spsc<InA>();
    auto [b_prod_perm, b_cons_perm] = split_spsc<InB>();
    auto [c_prod_perm, c_cons_perm] = split_spsc<InC>();
    auto [out_prod_perm, out_cons_perm] = split_spsc<Out>();
    (void)a_prod_perm;
    (void)b_prod_perm;
    (void)c_prod_perm;
    (void)out_cons_perm;

    auto a_cons = in_a.consumer(std::move(a_cons_perm));
    auto b_cons = in_b.consumer(std::move(b_cons_perm));
    auto c_cons = in_c.consumer(std::move(c_cons_perm));
    auto out_prod = out.producer(std::move(out_prod_perm));

    auto a_ep = cc::mint_endpoint<InA, cc::Direction::Consumer>(ctx, a_cons);
    auto b_ep = cc::mint_endpoint<InB, cc::Direction::Consumer>(ctx, b_cons);
    auto c_ep = cc::mint_endpoint<InC, cc::Direction::Consumer>(ctx, c_cons);
    auto out_ep = cc::mint_endpoint<Out, cc::Direction::Producer>(ctx, out_prod);

    auto stage = cc::mint_mpmc_stage_from_endpoints<&fan_in_body>(
        ctx,
        std::move(a_ep),
        std::move(b_ep),
        std::move(c_ep),
        std::move(out_ep));

    using Stage = decltype(stage);
    static_assert(cc::IsStage<Stage>);
    static_assert(cc::stage_input_count_v<Stage> == 3);
    static_assert(cc::stage_output_count_v<Stage> == 1);

    std::move(stage).run();
    require(fan_in_calls.load(std::memory_order_relaxed) == 1,
            "fan-in MPMC stage did not run exactly once");
}

static void test_mpmc_stage_from_endpoints_fan_out() {
    reset();
    eff::HotFgCtx ctx{};

    InA in_a;
    Out out;
    Out2 out2;

    auto [in_prod_perm, in_cons_perm] = split_spsc<InA>();
    auto [out_prod_perm, out_cons_perm] = split_spsc<Out>();
    auto [out2_prod_perm, out2_cons_perm] = split_spsc<Out2>();
    (void)in_prod_perm;
    (void)out_cons_perm;
    (void)out2_cons_perm;

    auto in_cons = in_a.consumer(std::move(in_cons_perm));
    auto out_prod = out.producer(std::move(out_prod_perm));
    auto out2_prod = out2.producer(std::move(out2_prod_perm));

    auto in_ep = cc::mint_endpoint<InA, cc::Direction::Consumer>(ctx, in_cons);
    auto out_ep = cc::mint_endpoint<Out, cc::Direction::Producer>(ctx, out_prod);
    auto out2_ep = cc::mint_endpoint<Out2, cc::Direction::Producer>(ctx, out2_prod);

    auto stage = cc::mint_mpmc_stage_from_endpoints<&fan_out_body>(
        ctx,
        std::move(in_ep),
        std::move(out_ep),
        std::move(out2_ep));

    using Stage = decltype(stage);
    static_assert(cc::IsStage<Stage>);
    static_assert(cc::stage_input_count_v<Stage> == 1);
    static_assert(cc::stage_output_count_v<Stage> == 2);

    std::move(stage).run();
    require(fan_out_calls.load(std::memory_order_relaxed) == 1,
            "fan-out MPMC stage did not run exactly once");
}

static void test_swmr_fan_out_graph() {
    reset();
    eff::HotFgCtx ctx{};

    SwmrIn in;
    Snapshot snapshot;

    auto [in_prod_perm, in_cons_perm] = split_spsc<SwmrIn>();
    (void)in_prod_perm;
    auto in_cons = in.consumer(std::move(in_cons_perm));
    auto in_ep = cc::mint_endpoint<SwmrIn, cc::Direction::Consumer>(
        ctx,
        in_cons);

    auto writer_perm = saf::mint_permission_root<Snapshot::writer_tag>();
    auto writer = snapshot.writer(std::move(writer_perm));

    auto source = cc::mint_swmr_stage<&swmr_publish_body>(
        ctx,
        std::move(in_ep),
        std::move(writer));

    auto reader_a = cc::mint_stage<&one_to_one_body>(
        ctx,
        FakeConsumer<int>{},
        FakeProducer<int>{});
    auto reader_b = cc::mint_stage<&one_to_one_body>(
        ctx,
        FakeConsumer<int>{},
        FakeProducer<int>{});
    auto reader_c = cc::mint_stage<&one_to_one_body>(
        ctx,
        FakeConsumer<int>{},
        FakeProducer<int>{});

    using Source = decltype(source);
    using Graph = cc::StageGraph<
        cc::StagePack<Source, PlainStage, PlainStage, PlainStage>,
        cc::EdgePack<cc::StageEdge<0, 1>,
                     cc::StageEdge<0, 2>,
                     cc::StageEdge<0, 3>>>;
    static_assert(cc::StageGraphWellFormed<Graph>);
    static_assert(cc::CtxFitsPipelineDag<eff::HotFgCtx, Graph>);

    auto pipeline = cc::mint_pipeline_dag(
        ctx,
        Graph{},
        std::move(source),
        std::move(reader_a),
        std::move(reader_b),
        std::move(reader_c));
    std::move(pipeline).run();

    require(swmr_calls.load(std::memory_order_relaxed) == 1,
            "SWMR source stage did not run");
    auto reader = snapshot.reader();
    require(reader.has_value(),
            "SWMR snapshot reader could not be minted after pipeline run");
    require(reader->load() == 7,
            "SWMR source stage did not publish into the snapshot");
    require(dag_calls.load(std::memory_order_relaxed) == 3,
            "SWMR fan-out readers did not all run");
}

static void test_diamond_dag_runtime() {
    reset();
    eff::HotFgCtx ctx{};

    auto s0 = cc::mint_stage<&one_to_one_body>(
        ctx,
        FakeConsumer<int>{},
        FakeProducer<int>{});
    auto s1 = cc::mint_stage<&one_to_one_body>(
        ctx,
        FakeConsumer<int>{},
        FakeProducer<int>{});
    auto s2 = cc::mint_stage<&one_to_one_body>(
        ctx,
        FakeConsumer<int>{},
        FakeProducer<int>{});
    auto s3 = cc::mint_stage<&one_to_one_body>(
        ctx,
        FakeConsumer<int>{},
        FakeProducer<int>{});

    using Graph = cc::StageGraph<
        cc::StagePack<PlainStage, PlainStage, PlainStage, PlainStage>,
        cc::EdgePack<cc::StageEdge<0, 1>,
                     cc::StageEdge<0, 2>,
                     cc::StageEdge<1, 3>,
                     cc::StageEdge<2, 3>>>;
    using Cycle = cc::StageGraph<
        cc::StagePack<PlainStage, PlainStage>,
        cc::EdgePack<cc::StageEdge<1, 0>>>;
    using Unreachable = cc::StageGraph<
        cc::StagePack<PlainStage, PlainStage, PlainStage>,
        cc::EdgePack<cc::StageEdge<0, 1>>>;

    static_assert(cc::StageGraphWellFormed<Graph>);
    static_assert(!cc::StageGraphWellFormed<Cycle>);
    static_assert(!cc::StageGraphWellFormed<Unreachable>);

    auto pipeline = cc::mint_pipeline_dag(
        ctx,
        Graph{},
        std::move(s0),
        std::move(s1),
        std::move(s2),
        std::move(s3));
    std::move(pipeline).run();

    require(dag_calls.load(std::memory_order_relaxed) == 4,
            "diamond DAG did not run all four stages");
}

}  // namespace pipeline_dag_test

int main() {
    pipeline_dag_test::test_mpmc_stage_from_endpoints_fan_in();
    pipeline_dag_test::test_mpmc_stage_from_endpoints_fan_out();
    pipeline_dag_test::test_swmr_fan_out_graph();
    pipeline_dag_test::test_diamond_dag_runtime();
    std::fprintf(stderr, "test_pipeline_dag: ALL PASSED\n");
    return EXIT_SUCCESS;
}
