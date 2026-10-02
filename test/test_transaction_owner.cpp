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
#include "test_harness.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <type_traits>
#include "test_assert.h"

using crucible::SchemaHash;
using crucible::ShapeHash;

namespace {

// Each call on the stage gets its own proof, by value, and the proof dies
// when the call returns.  A job or a callback that keeps the address of
// its proof keeps an invalid pointer, and no later use of it is legal
// C++, so no test runs one.  These assertions pin the shape that makes
// that true: no signature takes the proof by reference.
using Stage = crucible::BackgroundThread::PublishStage;
static_assert(std::is_same_v<crucible::BackgroundThread::RegionReadyCallback::Fn,
                             void (*)(void*, ::foundation::effects::Bg const&, Stage, crucible::RegionNode*) noexcept>);
static_assert(std::is_same_v<decltype(crucible::BackgroundThread::OwnerJob::fn), void (*)(void*, Stage) noexcept>);
static_assert(!std::is_copy_constructible_v<Stage> && !std::is_move_constructible_v<Stage>);

constexpr uint32_t kOps = 8;
constexpr uint32_t kSignature = crucible::Vigil::ALIGNMENT_K;

struct OpData {
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta metas[2]{};
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
    if (op_idx > 0) made.metas[idx++] = meta_at(fake_ptr(iter, op_idx - 1));
    made.metas[idx++] = meta_at(fake_ptr(iter, op_idx));
    made.n_metas = static_cast<uint16_t>(made.entry.num_inputs + made.entry.num_outputs);
    return made;
}

// Two whole iterations and one signature-length head, the shortest input
// that makes the detector confirm a boundary and publish a region.
void feed_one_region(crucible::Vigil& vigil, uint32_t first_iter) {
    for (uint32_t iter = first_iter; iter < first_iter + 2; iter++) {
        for (uint32_t i = 0; i < kOps; i++) {
            auto recorded = op_at(iter, i);
            (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                                  recorded.n_metas);
        }
    }
    for (uint32_t i = 0; i < kSignature; i++) {
        auto recorded = op_at(first_iter + 2, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas,
                              recorded.n_metas);
    }
}

[[noreturn]] void fail(const char* what) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
}

// Each rollback here is issued while the background thread may still be
// publishing the region fed just before it, so the log sees a rollback and
// a publication close together on every round.
void rollback_while_regions_are_in_flight() {
    crucible::Vigil vigil;
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(vigil);

    constexpr uint32_t kRounds = 24;
    for (uint32_t round = 1; round <= kRounds; ++round) {
        feed_one_region(vigil, round * 4);
        (void)vigil.rollback();
    }
    vigil.flush();

    // With the pipeline idle, two more regions leave one superseded entry,
    // and a rollback must restore it.
    feed_one_region(vigil, 1000);
    vigil.flush();
    feed_one_region(vigil, 1004);
    vigil.flush();
    if (!vigil.rollback()) fail("a rollback with a superseded transaction restored nothing");
}

void rollback_on_a_fresh_vigil() {
    crucible::Vigil vigil;
    if (vigil.rollback()) fail("a rollback on an empty log restored a transaction");
}

// A background thread that never started has no publish stage, so a job
// runs on its caller, who owns the state while no stage runs.  Two jobs in
// a row show that the inline path gives the mailbox back.
void job_runs_on_the_caller_without_a_stage() {
    crucible::BackgroundThread background;
    int runs = 0;
    background.run_on_publish_stage([&runs](crucible::BackgroundThread::PublishStage const&) noexcept { ++runs; });
    background.run_on_publish_stage([&runs](crucible::BackgroundThread::PublishStage const&) noexcept { ++runs; });
    if (runs != 2) fail("a job posted with no publish stage did not run on its caller");
}

std::thread::id thread_of_one_job(crucible::BackgroundThread& background) {
    std::thread::id ran_on{};
    background.run_on_publish_stage(
        [&ran_on](crucible::BackgroundThread::PublishStage const&) noexcept { ran_on = std::this_thread::get_id(); });
    return ran_on;
}

// While the publish stage runs, a job runs on the stage's thread.  The
// stage opens its mailbox as its first act, so a job posted before that
// runs on the caller, and the loop waits for the first job the stage runs.
// After stop() the stage has closed the mailbox, and a job runs on its
// caller again.
void job_runs_on_the_stage_while_it_runs() {
    auto ring = std::make_unique<crucible::TraceRing>();
    auto meta_log = std::make_unique<crucible::MetaLog>();
    crucible::BackgroundThread background;
    background.start(ring.get(), meta_log.get());

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
// on itself, because the thread that runs it holds the stage.  The contract
// ends the process instead of letting it hang.
void nested_job_ends_the_process() {
    crucible::BackgroundThread background;
    background.run_on_publish_stage([&background](crucible::BackgroundThread::PublishStage const&) noexcept {
        background.run_on_publish_stage([](crucible::BackgroundThread::PublishStage const&) noexcept {});
    });
}

[[nodiscard]] bool ends_the_process(void (*attack)()) {
    std::fflush(stderr);
    // SPAWN-PROCESS-OK: a refused call ends the process, so it runs in a
    // child that the parent observes.
    const pid_t pid = ::fork();  // SPAWN-PROCESS-OK: death test, see above
    if (pid < 0) fail("fork failed");
    if (pid == 0) {
        attack();
        std::_Exit(0);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) fail("waitpid failed");  // SPAWN-PROCESS-OK: death test, see above
    return WIFSIGNALED(status) != 0;
}

}  // namespace

int main() {
    job_runs_on_the_caller_without_a_stage();
    if (!ends_the_process(&nested_job_ends_the_process)) fail("a nested job did not end the process");
    rollback_on_a_fresh_vigil();
    rollback_while_regions_are_in_flight();
    job_runs_on_the_stage_while_it_runs();
    crucible::test::pass("test_transaction_owner: passed\n");
    return 0;
}
