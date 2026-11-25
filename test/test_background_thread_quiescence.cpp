// A proof of quiescence (fixy/os/OwnedThread.h) names one BackgroundThread,
// and it reads the state of the pipeline thread at each use.  The type of a
// proof does not name its object.  So each door that takes a proof ends the
// process for the proof of another BackgroundThread, for a proof while the
// pipeline thread runs, and while a run of run_in_row is active.
//
// The target test_background_thread_quiescence_ignore compiles this file with
// the ignore semantic, where a CRUCIBLE_PRE is only an assumption.  Each check
// must hold there too.  Each broken use runs in a child process through
// expect_dies of the Check family, while the test process has one thread.  The
// children share the two objects of main, each in its own copy of the process.

#include <crucible/BackgroundThread.h>
#include <crucible/MetaLog.h>
#include <crucible/TraceRing.h>
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/OwnedThread.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include "test_harness.h"
#include "trace_rig.h"

#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

using crucible::BackgroundThread;
using crucible::MetaIndex;
using crucible::TraceRing;
using crucible::test::TraceRig;
using Proof = ::fixy::thread::QuiescenceProof<BackgroundThread>;

constexpr uint64_t kOps = 40;

constexpr ::fixy::TextView kNotItsProof{"fatal invariant failed: proof.holds_part(thread_) && proof.is_quiescent()"};
constexpr ::fixy::TextView kActiveRun{"fatal invariant failed: pipeline_run_.load_acquire() == PipelineRun::stopped"};

// The doors that take a proof of quiescence.
enum class ProofDoor : uint8_t {
    run_in_row,
    trace_build_state,
    detector_history_count,
    pending_op_count,
};

[[nodiscard]] ::fixy::InitLoadCtx startup() noexcept {
    return ::fixy::InitLoadCtx{::foundation::effects::testing::init()};
}

[[nodiscard]] ::fixy::BgLoadCtx background() noexcept {
    return ::fixy::BgLoadCtx{::foundation::effects::testing::bg()};
}

void call_door(BackgroundThread& bt, Proof const& proof, ProofDoor door) noexcept {
    if (door == ProofDoor::run_in_row) return bt.run_in_row(proof, background());
    if (door == ProofDoor::trace_build_state) return static_cast<void>(bt.trace_build_state(proof));
    if (door == ProofDoor::detector_history_count) return static_cast<void>(bt.detector_history_count(proof));
    static_cast<void>(bt.pending_op_count(proof));
}

// A proof of its own object, taken while no pipeline runs, opens each door.
// The stop signal ends the run of run_in_row.
void test_own_proof_opens_each_door(BackgroundThread& bt) {
    Proof const proof = bt.prove_quiescence();
    CRUCIBLE_TEST_ASSERT(kTest, bt.pending_op_count(proof) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, bt.detector_history_count(proof) == 0);
    call_door(bt, proof, ProofDoor::trace_build_state);
    bt.stop_requested.signal();
    call_door(bt, proof, ProofDoor::run_in_row);
    CRUCIBLE_TEST_ASSERT(kTest, bt.pending_op_count(proof) == 0);
}

// The proof of another BackgroundThread ends the process at each door.  The
// stop signal ends a run that a door with no check would start.
void test_foreign_proof_ends_the_process(BackgroundThread& bt, BackgroundThread& other, ProofDoor door) {
    ::fixy::check::expect_dies(
        kTest,
        [&bt, &other, door] noexcept {
            bt.stop_requested.signal();
            call_door(bt, other.prove_quiescence(), door);
        },
        kNotItsProof);
}

// A proof that outlives the start of the pipeline thread ends the process at
// each door.
void test_proof_of_a_running_thread_ends_the_process(BackgroundThread& bt, ProofDoor door) {
    ::fixy::check::expect_dies(
        kTest,
        [&bt, door] noexcept {
            Proof const proof = bt.prove_quiescence();
            bt.start(startup());
            call_door(bt, proof, door);
        },
        kNotItsProof);
}

// A run of run_in_row on a thread of the test holds no pipeline thread, so the
// proof names no thread that runs.  The run state ends the process.  The reader
// calls its door after the pipeline processed the ops, so the run is active.
// The stop signal ends the run when a door has no check.
void test_active_run_ends_the_process(TraceRig& rig, BackgroundThread& bt, ProofDoor door) {
    ::fixy::check::expect_dies(
        kTest,
        [&rig, &bt, door] noexcept {
            for (uint64_t index = 0; index < kOps; ++index) {
                TraceRing::Entry entry{};
                entry.schema_hash = crucible::SchemaHash{0x300 + index % 5};
                entry.shape_hash = crucible::ShapeHash{0x400 + index % 5};
                CRUCIBLE_TEST_ASSERT(kTest, rig.append_entry(entry, MetaIndex::none()));
            }
            bt.rearm_stop();
            auto run = [&bt]() noexcept { bt.run_in_row(bt.prove_quiescence(), background()); };
            auto read = [&bt, door]() noexcept {
                while (bt.total_processed.load_acquire() < kOps) {
                    CRUCIBLE_SPIN_PAUSE;
                }
                call_door(bt, bt.prove_quiescence(), door);
                bt.stop_requested.signal();
            };
            ::fixy::BgLoadCtx const bg = background();
            static_cast<void>(
                ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(run), std::move(read)));
        },
        kActiveRun);
}

}  // namespace

int main() {
    TraceRig rig{};
    TraceRig other_rig{};
    BackgroundThread bt{::foundation::effects::testing::test().alloc, rig.ring_side(), rig.log_side()};
    BackgroundThread other{::foundation::effects::testing::test().alloc, other_rig.ring_side(), other_rig.log_side()};
    test_foreign_proof_ends_the_process(bt, other, ProofDoor::run_in_row);
    test_foreign_proof_ends_the_process(bt, other, ProofDoor::trace_build_state);
    test_foreign_proof_ends_the_process(bt, other, ProofDoor::detector_history_count);
    test_foreign_proof_ends_the_process(bt, other, ProofDoor::pending_op_count);
    test_proof_of_a_running_thread_ends_the_process(bt, ProofDoor::run_in_row);
    test_proof_of_a_running_thread_ends_the_process(bt, ProofDoor::trace_build_state);
    test_proof_of_a_running_thread_ends_the_process(bt, ProofDoor::detector_history_count);
    test_proof_of_a_running_thread_ends_the_process(bt, ProofDoor::pending_op_count);
    test_active_run_ends_the_process(rig, bt, ProofDoor::trace_build_state);
    test_active_run_ends_the_process(rig, bt, ProofDoor::detector_history_count);
    test_active_run_ends_the_process(rig, bt, ProofDoor::pending_op_count);
    test_own_proof_opens_each_door(bt);
    ::fixy::check::pass(kTest, "test_background_thread_quiescence: all passed\n");
    return 0;
}
