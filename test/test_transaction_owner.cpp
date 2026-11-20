// The transaction log of a Vigil has one owner, the publish stage.
//
// on_region_ready opens, commits and activates a transaction for every
// region the publish stage publishes.  Vigil::rollback() used to write the
// same log from the foreground, so a rollback that ran while a region was
// in flight raced the publish stage on plain reads and writes.  The thread
// sanitizer preset reported that race on this test.
//
// The log now takes the publish stage's proof on every member, and the
// foreground rolls back by a job that runs on the publish stage between two
// publications.  This test keeps regions in flight while it rolls back, and
// under the thread sanitizer preset it runs with no report.  In every build
// it checks that a rollback still restores a region.  It also checks where
// a job runs: on its caller while no publish stage runs, on the stage while
// one runs, and nowhere when a job posts a job, which ends the process.

#include <crucible/Vigil.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>
#include "test_harness.h"

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <thread>
#include <type_traits>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

using crucible::SchemaHash;
using crucible::ShapeHash;

namespace {

// Each call on the stage gets its own proof, and the proof dies when the
// call returns.  A job or a callback that keeps the address of its proof
// keeps an invalid pointer, and no later use of it is legal C++, so no test
// runs one.  These assertions pin the shape that makes that true: the
// region callback and the call of a job each take a reference to a
// temporary proof that the caller builds for that call.
using Stage = crucible::BackgroundThread::PublishStage;
static_assert(std::is_same_v<crucible::BackgroundThread::RegionReadyCall,
                             ::fixy::CallRef<void(::foundation::effects::Bg const&, Stage const&,
                                                  crucible::BackgroundThread::PublishedRegion const&) noexcept>>);
static_assert(
    std::is_same_v<decltype(crucible::BackgroundThread::OwnerJob::call), ::fixy::CallRef<void(Stage const&) noexcept>>);
static_assert(!std::is_copy_constructible_v<Stage> && !std::is_move_constructible_v<Stage>);

constexpr uint32_t kOps = 8;
constexpr uint32_t kSignature = crucible::Vigil::ALIGNMENT_K;

struct OpData {
    crucible::TraceRing::Entry entry{};
    ::fixy::FixedArray<crucible::TensorMeta, 2> metas{};
    uint16_t n_metas = 0;
};

void* fake_ptr(uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

crucible::TensorMeta meta_at(void* data_ptr) {
    crucible::TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(1024);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx = 0;
    m.layout = crucible::Layout::Strided;
    m.data_ptr = crucible::external_data_ptr(data_ptr);
    return m;
}

OpData op_at(uint32_t iter, uint32_t op_idx) {
    OpData made;
    made.entry.schema_hash = SchemaHash{0x300 + op_idx};
    made.entry.shape_hash = ShapeHash{0x400 + op_idx};
    made.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    made.entry.num_outputs = 1;
    uint16_t idx = 0;
    if (op_idx > 0)
        *made.metas.get(idx++).expect("a test reads an element at or past the count of its array") =
            meta_at(fake_ptr(iter, op_idx - 1));
    *made.metas.get(idx++).expect("a test reads an element at or past the count of its array") =
        meta_at(fake_ptr(iter, op_idx));
    made.n_metas = static_cast<uint16_t>(made.entry.num_inputs + made.entry.num_outputs);
    return made;
}

// Two whole iterations and one signature-length head, the shortest input
// that makes the detector confirm a boundary and publish a region.
void feed_one_region(crucible::Vigil& vigil, uint32_t first_iter) {
    for (uint32_t iter = first_iter; iter < first_iter + 2; iter++) {
        for (uint32_t i = 0; i < kOps; i++) {
            auto recorded = op_at(iter, i);
            (void)vigil.record_op(
                crucible::test::certify_synthetic_op(recorded.entry, recorded.metas, recorded.n_metas));
        }
    }
    for (uint32_t i = 0; i < kSignature; i++) {
        auto recorded = op_at(first_iter + 2, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_op(recorded.entry, recorded.metas, recorded.n_metas));
    }
}

[[noreturn]] void fail(::fixy::TextView what) { ::fixy::fatal("FAIL: {}\n", what); }

// Each rollback here is issued while the background thread may still be
// publishing the region fed just before it, so the log sees a rollback and
// a publication close together on every round.
void rollback_while_regions_are_in_flight() {
    crucible::Vigil vigil;
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(kTest, vigil);

    constexpr uint32_t kRounds = 24;
    for (uint32_t round = 1; round <= kRounds; ++round) {
        feed_one_region(vigil, round * 4);
        (void)vigil.rollback();
    }
    CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());

    // With the pipeline idle, two more regions leave one superseded entry,
    // and a rollback must restore it.
    feed_one_region(vigil, 1000);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());
    feed_one_region(vigil, 1004);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());
    if (!vigil.rollback()) fail("a rollback with a superseded transaction restored nothing");
}

// The publish stage sweeps the store of regions at each publication, and a
// rollback gives its region to the foreground between two publications.  Each
// round feeds a region, so the stage publishes and sweeps while the
// foreground rolls back, and then the foreground reads each op of the region
// that it holds.  THE LIFE OF A REGION of BackgroundThread.h keeps the region
// in the store until the foreground writes its holders again.  A read of a
// freed region shows under the address sanitizer, and a race of the sweep and
// the read shows under the thread sanitizer preset.
void rollback_reads_its_region_while_the_store_sweeps() {
    crucible::Vigil vigil;
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(kTest, vigil);
    feed_one_region(vigil, 4);
    crucible::test::flush_and_wait_region_published(kTest, vigil);

    constexpr uint32_t kRounds = 32;
    uint32_t restored = 0;
    for (uint32_t round = 2; round < 2 + kRounds; ++round) {
        feed_one_region(vigil, round * 4);
        if (!vigil.rollback()) continue;
        ++restored;
        for (const ::fixy::Ref<const crucible::RegionNode> region : vigil.active_region()) {
            for (const crucible::TraceEntry& op : region->ops) {
                const uint64_t schema = op.schema_hash.raw();
                if (schema < 0x300 || schema >= 0x300 + kOps) fail("a region read after a rollback holds a foreign op");
            }
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, vigil.flush(kTest).is_ok());
    if (restored == 0) fail("no rollback restored a region while regions were in flight");
}

void rollback_on_a_fresh_vigil() {
    crucible::Vigil vigil;
    if (vigil.rollback()) fail("a rollback on an empty log restored a transaction");
}

// A background thread that never started has no publish stage, so a job
// runs on its caller, who owns the state while no stage runs.  Two jobs in
// a row show that the inline path gives the mailbox back.
void job_runs_on_the_caller_without_a_stage() {
    crucible::BackgroundThread background{::foundation::effects::testing::test().alloc};
    int runs = 0;
    background.run_on_publish_stage([&runs](crucible::BackgroundThread::PublishStageView&,
                                            crucible::BackgroundThread::PublishStage const&) noexcept { ++runs; });
    background.run_on_publish_stage([&runs](crucible::BackgroundThread::PublishStageView&,
                                            crucible::BackgroundThread::PublishStage const&) noexcept { ++runs; });
    if (runs != 2) fail("a job posted with no publish stage did not run on its caller");
}

std::thread::id thread_of_one_job(crucible::BackgroundThread& background) {
    std::thread::id ran_on{};
    background.run_on_publish_stage(
        [&ran_on](crucible::BackgroundThread::PublishStageView&,
                  crucible::BackgroundThread::PublishStage const&) noexcept { ran_on = std::this_thread::get_id(); });
    return ran_on;
}

// While the publish stage runs, a job runs on the stage's thread.  The
// stage opens its mailbox as its first act, so a job posted before that
// runs on the caller, and the loop waits for the first job the stage runs.
// After stop() the stage has closed the mailbox, and a job runs on its
// caller again.
void job_runs_on_the_stage_while_it_runs() {
    auto ring = std::make_unique<crucible::TraceRing>();
    auto meta_log = std::make_unique<crucible::MetaLog>(::foundation::effects::testing::test().alloc);
    crucible::BackgroundThread background{::foundation::effects::testing::test().alloc};
    background.start(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, *ring, *meta_log);

    const std::thread::id caller = std::this_thread::get_id();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    bool ran_on_the_stage = false;
    while (!ran_on_the_stage && std::chrono::steady_clock::now() < deadline) {
        ran_on_the_stage = thread_of_one_job(background) != caller;
    }
    if (!ran_on_the_stage) fail("no job ran on the publish stage while it ran");

    background.stop();
    if (thread_of_one_job(background) != caller) fail("a job after stop() did not run on its caller");
}

// A job that posts a second job to the same background thread would wait
// on itself, because the thread that runs it holds the stage.  The view of a
// job has no door that posts a job, so the job reaches the background thread
// through a Ref that it captures.  The contract ends the process instead of
// letting it hang.  The refused call runs in a child through expect_dies of
// the Check family, before the test starts a thread.
void nested_job_ends_the_process() {
    crucible::BackgroundThread background{::foundation::effects::testing::test().alloc};
    background.run_on_publish_stage(
        [owner = ::fixy::to(background)](crucible::BackgroundThread::PublishStageView&,
                                         crucible::BackgroundThread::PublishStage const&) noexcept {
            owner->run_on_publish_stage([](crucible::BackgroundThread::PublishStageView&,
                                           crucible::BackgroundThread::PublishStage const&) noexcept {});
        });
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    job_runs_on_the_caller_without_a_stage();
    ::fixy::check::expect_dies(test, nested_job_ends_the_process,
                               ::fixy::TextView{"foundation: contract violation: stage_held_by_this_thread_"});
    rollback_on_a_fresh_vigil();
    rollback_while_regions_are_in_flight();
    rollback_reads_its_region_while_the_store_sweeps();
    job_runs_on_the_stage_while_it_runs();
    ::fixy::check::pass(test, "test_transaction_owner: passed\n");
    return 0;
}
