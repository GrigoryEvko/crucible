#include <crucible/Vigil.h>
#include "test_harness.h"
#include "test_assert.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using crucible::SchemaHash;
using crucible::ShapeHash;

static crucible::TraceRing::Entry make_entry(SchemaHash schema_hash) {
    crucible::TraceRing::Entry e{};
    e.schema_hash = schema_hash;
    e.shape_hash = ShapeHash{0x1234};
    e.num_inputs = 1;
    e.num_outputs = 1;
    e.num_scalar_args = 0;
    e.op_flags = 0;
    return e;
}

static crucible::TensorMeta make_meta() {
    crucible::TensorMeta m{};
    m.ndim = 1;
    m.sizes[0] = ::crucible::tensor_dim(8);
    m.strides[0] = ::crucible::tensor_dim(1);
    m.dtype = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx = -1;
    m.layout = crucible::Layout::Strided;
    m.data_ptr = crucible::external_data_ptr(nullptr);
    return m;
}

static std::filesystem::path object_path_for(const std::filesystem::path& root, crucible::ContentHash hash) {
    char hex[16];
    uint64_t value = hash.raw();
    static constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 16; ++i) {
        hex[15 - i] = kHex[value & 0x0FULL];
        value >>= 4;
    }
    return root / "objects" / std::string(hex, 2) / std::string(hex + 2, 14);
}

// ─────────────────────────────────────────────────────────────────────
// A divergence must leave no published region behind
//
// While the context is compiled, dispatch_op takes the replay branch and
// never reads the publication slot.  A region the background thread
// publishes during that window therefore sits there unobserved.  It was cut
// from entries recorded before the divergence, so a divergence that leaves
// it in place hands it to the very next dispatch, which starts aligning
// against the trace the divergence just invalidated.  Alignment records
// nothing, so the background thread never receives the new trace either.
//
// The window opens deterministically below.  The ring is stuffed through
// record_op while the context is compiled, so the background thread
// publishes a second region the foreground cannot see.  flush() then leaves
// the ring empty and the background thread idle, and the diverging op is
// deliberately not recorded, so no further region can appear between the
// divergence and the dispatch that follows it.
// ─────────────────────────────────────────────────────────────────────
namespace divrig {

static constexpr uint32_t NUM_OPS = 8;
static constexpr uint32_t K = crucible::Vigil::ALIGNMENT_K;

static constexpr SchemaHash SCHEMA[NUM_OPS] = {SchemaHash{0x100}, SchemaHash{0x101}, SchemaHash{0x102},
                                               SchemaHash{0x103}, SchemaHash{0x104}, SchemaHash{0x105},
                                               SchemaHash{0x106}, SchemaHash{0x107}};
static constexpr ShapeHash SHAPE[NUM_OPS] = {ShapeHash{0x200}, ShapeHash{0x201}, ShapeHash{0x202}, ShapeHash{0x203},
                                             ShapeHash{0x204}, ShapeHash{0x205}, ShapeHash{0x206}, ShapeHash{0x207}};

// A schema that appears in no region, so the divergence it causes finds no
// alternate in the region cache and falls through to the recording reset.
static constexpr SchemaHash FOREIGN_SCHEMA = SchemaHash{0x9999};

struct OpData {
    crucible::TraceRing::Entry entry{};
    crucible::TensorMeta metas[2]{};
    uint16_t n_metas = 0;
};

static void* fake_ptr(uint32_t iter, uint32_t op) {
    return std::bit_cast<void*>(static_cast<std::uintptr_t>((iter + 1) * 0x100000 + (op + 1) * 0x1000));
}

static crucible::TensorMeta meta_at(void* data_ptr) {
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

static OpData op_at(uint32_t iter, uint32_t op_idx) {
    OpData made;
    made.entry.schema_hash = SCHEMA[op_idx];
    made.entry.shape_hash = SHAPE[op_idx];
    made.entry.num_inputs = (op_idx == 0) ? 0 : 1;
    made.entry.num_outputs = 1;

    uint16_t idx = 0;
    if (op_idx > 0) made.metas[idx++] = meta_at(fake_ptr(iter, op_idx - 1));
    made.metas[idx++] = meta_at(fake_ptr(iter, op_idx));
    made.n_metas = static_cast<uint16_t>(made.entry.num_inputs + made.entry.num_outputs);
    return made;
}

// Two whole iterations and one signature-length head, which is the shortest
// input that makes the detector confirm a boundary and publish a region.
static void feed_one_region(crucible::Vigil& vigil, uint32_t first_iter) {
    for (uint32_t iter = first_iter; iter < first_iter + 2; iter++) {
        for (uint32_t i = 0; i < NUM_OPS; i++) {
            auto recorded = op_at(iter, i);
            (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas, recorded.n_metas);
        }
    }
    for (uint32_t i = 0; i < K; i++) {
        auto recorded = op_at(first_iter + 2, i);
        (void)vigil.record_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas, recorded.n_metas);
    }
}

}  // namespace divrig

static void test_divergence_drops_the_unobserved_region() {
    using namespace divrig;
    using crucible::DispatchResult;
    using crucible::ReplayStatus;

    crucible::Vigil vigil;

    // Phase 1: the background thread publishes a region and the foreground
    // aligns onto it, which leaves the context compiled.
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(vigil);

    for (uint32_t i = 0; i < K; i++) {
        auto recorded = op_at(3, i);
        auto result = vigil.dispatch_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD && "alignment ops return RECORD");
    }
    assert(vigil.context().is_compiled() && "the context must be compiled after K alignment ops");
    for (uint32_t i = K; i < NUM_OPS; i++) {
        auto recorded = op_at(3, i);
        auto result = vigil.dispatch_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::COMPILED);
    }

    const uint64_t step_after_first = vigil.current_step();
    assert(step_after_first >= 1 && "the background thread must have published one region");

    // Phase 2: open the window.  These entries reach the ring while the
    // context is compiled, so the region the background thread publishes for
    // them lands in a slot the replay branch of dispatch_op never reads.
    feed_one_region(vigil, 4);
    vigil.flush();
    assert(vigil.flush_complete() && "flush() returned before the background thread finished");
    assert(vigil.current_step() > step_after_first
           && "the background thread must have published a second region into the unobserved slot");
    assert(vigil.context().is_compiled() && "the foreground must still be compiled: it never read the slot");

    // Phase 3: diverge.  The diverging op is not recorded, and the ring is
    // empty, so the background thread cannot publish anything between here
    // and the dispatch below.
    const uint32_t diverged_before = vigil.diverged_count();
    OpData foreign_op = op_at(7, 0);
    foreign_op.entry.schema_hash = FOREIGN_SCHEMA;
    auto result_diverged = vigil.dispatch_op(crucible::test::certify_synthetic_entry(foreign_op.entry), foreign_op.metas, foreign_op.n_metas);
    assert(result_diverged.action == DispatchResult::Action::RECORD && "a divergence falls back to recording");
    assert(result_diverged.status == ReplayStatus::DIVERGED);
    assert(vigil.diverged_count() > diverged_before);
    assert(!vigil.context().is_compiled() && "the context must be deactivated by the divergence");

    // The claim.  The next op belongs to a trace the background thread has
    // not seen yet, so it must reach the ring.  If the unobserved region is
    // still in the slot, this op is consumed by alignment instead and the
    // ring never grows.
    const uint64_t produced_before = vigil.ring().total_produced();
    auto after_divergence = op_at(7, 0);
    auto result_after =
        vigil.dispatch_op(crucible::test::certify_synthetic_entry(after_divergence.entry), after_divergence.metas, after_divergence.n_metas);
    assert(result_after.action == DispatchResult::Action::RECORD);
    assert(vigil.ring().total_produced() == produced_before + 1
           && "the first op after a divergence must be recorded, not aligned against the region the "
              "background thread published while the context was compiled");

    std::printf("  test_divergence_drops_the_unobserved_region: PASSED\n");
}

// The other half of the same defect: the alignment walk, rather than the
// publication slot.
//
// A walk lives in two foreground members that no divergence used to clear.
// rollback() activates a context without looking at them, so a walk that is
// half done survives into compiled mode, and the divergence that follows
// leaves it there.  The next dispatch then resumes a walk into a region the
// divergence abandoned, and records nothing while it does.
//
// The walk is opened here by dispatching two ops of a published region and
// stopping short of the K that would activate it.  rollback() then supplies
// the compiled context, which is what makes a divergence reachable at all.
static void test_divergence_drops_a_half_finished_alignment() {
    using namespace divrig;
    using crucible::DispatchResult;
    using crucible::ReplayStatus;

    crucible::Vigil vigil;

    // Two published regions, so the transaction log has one superseded
    // entry for rollback() to restore.  Neither is observed by the
    // foreground: these entries reach the ring through record_op.
    feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(vigil);
    const uint64_t step_after_first = vigil.current_step();

    feed_one_region(vigil, 4);
    vigil.flush();
    assert(vigil.current_step() > step_after_first && "a second region must be published");

    // Open a walk two ops deep and stop.  K is 5, so this does not activate.
    for (uint32_t i = 0; i < 2; i++) {
        auto recorded = op_at(8, i);
        auto result = vigil.dispatch_op(crucible::test::certify_synthetic_entry(recorded.entry), recorded.metas, recorded.n_metas);
        assert(result.action == DispatchResult::Action::RECORD);
    }
    assert(!vigil.context().is_compiled() && "two ops are short of the K that activates");

    // Compile the context out from under the open walk.
    assert(vigil.rollback() && "rollback() needs the superseded transaction the second region left");
    assert(vigil.context().is_compiled() && "rollback() must activate the restored region");

    OpData foreign_op = op_at(9, 0);
    foreign_op.entry.schema_hash = FOREIGN_SCHEMA;
    auto result_diverged = vigil.dispatch_op(crucible::test::certify_synthetic_entry(foreign_op.entry), foreign_op.metas, foreign_op.n_metas);
    assert(result_diverged.status == ReplayStatus::DIVERGED);
    assert(!vigil.context().is_compiled());

    // The claim.  This op continues the abandoned walk if the walk is still
    // open, and reaches the ring if it is not.
    const uint64_t produced_before = vigil.ring().total_produced();
    auto after_divergence = op_at(9, 2);
    auto result_after =
        vigil.dispatch_op(crucible::test::certify_synthetic_entry(after_divergence.entry), after_divergence.metas, after_divergence.n_metas);
    assert(result_after.action == DispatchResult::Action::RECORD);
    assert(vigil.ring().total_produced() == produced_before + 1
           && "the first op after a divergence must be recorded, not fed to an alignment walk the "
              "divergence abandoned");

    std::printf("  test_divergence_drops_a_half_finished_alignment: PASSED\n");
}

// The ring behind Vigil is single-producer.  record_op reaches it without
// going through dispatch_op, so it carries the same thread gate, and the
// first thread through it claims the producer role.
//
// Proving the claim happens proves the gate is on this entry point, and it
// needs no abort to observe: the claiming thread is joined before the role
// is read back, so the read carries a happens-before edge and the recorded
// op stream stays single-threaded throughout.
static void test_record_op_claims_the_producer_role() {
    crucible::Vigil vigil;
    assert(vigil.is_producer_thread() && "an unclaimed Vigil admits the first thread that arrives");

    std::thread claimer([&vigil] {
        auto first = divrig::op_at(0, 0);
        assert(vigil.record_op(crucible::test::certify_synthetic_entry(first.entry), first.metas, first.n_metas));
        assert(vigil.is_producer_thread() && "the thread that claimed the role holds it");
    });
    claimer.join();

    assert(!vigil.is_producer_thread()
           && "record_op must claim the producer role, which makes every later thread a second producer");

    std::printf("  test_record_op_claims_the_producer_role: PASSED\n");
}

int main() {
    test_divergence_drops_the_unobserved_region();
    test_divergence_drops_a_half_finished_alignment();
    test_record_op_claims_the_producer_role();

    char tmpdir[] = "/tmp/crucible_vigil_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr);

    crucible::Vigil::Config cfg;
    cfg.cipher_path = dir;
    crucible::Vigil vigil(std::move(cfg));

    assert(!vigil.is_compiled());
    assert(vigil.current_step() == 0);
    assert(vigil.active_region() == nullptr);

    // Fifteen ops, as three repeats of the same five schema hashes. The
    // detector needs a signature of five, then a first match, then a second
    // match before it confirms a boundary, so three repeats is the minimum
    // that produces a region. Each op carries two tensor metas, one in and
    // one out, or the trace cannot be rebuilt.

    const SchemaHash schemas[5] = {SchemaHash{0xAA01}, SchemaHash{0xBB02}, SchemaHash{0xCC03}, SchemaHash{0xDD04},
                                   SchemaHash{0xEE05}};
    const crucible::TensorMeta meta = make_meta();
    const crucible::TensorMeta io_metas[2] = {meta, meta};  // [0]=input, [1]=output

    for (int iter = 0; iter < 3; iter++) {
        for (int j = 0; j < 5; j++) {
            auto e = make_entry(schemas[j]);
            const bool ok = vigil.record_op(crucible::test::certify_synthetic_entry(e), io_metas, 2);
            assert(ok && "record_op must succeed (ring not full)");
        }
    }

    crucible::test::flush_and_wait_region_published(vigil);

    // A published region is not a replay. The mode turns COMPILED only when
    // the foreground aligns to the region and activates the context, and
    // this test dispatches nothing after the flush.
    assert(vigil.has_pending_region());
    assert(!vigil.is_compiled() && "publication alone must not report a replay");
    assert(!vigil.context().is_compiled());
    assert(vigil.active_region() != nullptr);
    assert(vigil.current_step() >= 1);
    const crucible::RegionNode* active_region = vigil.active_region();
    const auto object_path = object_path_for(dir, active_region->content_hash);
    assert(!std::filesystem::exists(object_path) && "background Vigil callback must not pre-store Cipher objects");
    assert(!std::filesystem::exists(std::string(dir) + "/HEAD")
           && "background Vigil callback must not advance Cipher HEAD");

    const bool persisted = vigil.persist();
    assert(persisted && "persist() must succeed with a cipher_path set");
    assert(static_cast<bool>(vigil.head_hash()) && "Cipher HEAD must be non-zero after persist()");
    assert(std::filesystem::exists(object_path) && "foreground persist() must be the direct Cipher object writer");

    std::ifstream hf(std::string(dir) + "/HEAD");
    assert(hf.is_open() && "HEAD file must exist on disk after persist()");
    std::string head_hex;
    std::getline(hf, head_hex);
    assert(!head_hex.empty() && "HEAD file must be non-empty");

    int region_exec_count = 0;
    const crucible::RegionNode* exec_region_ptr = nullptr;

    const bool replayed = vigil.replay(
        // Guard evaluation. The region is linear, so there is no guard.
        [](const crucible::Guard&) -> int64_t { return 0; },
        // Region execution.
        [&region_exec_count, &exec_region_ptr](const crucible::RegionNode* r) {
            region_exec_count++;
            exec_region_ptr = r;
        });

    assert(replayed && "replay() must return true for a linear region");
    assert(region_exec_count == 1 && "RegionExec must be called exactly once for a single-region DAG");
    assert(exec_region_ptr == vigil.active_region() && "replay() must execute the active region");

    std::filesystem::remove_all(dir);

    std::printf("test_vigil: all tests passed\n");
    return 0;
}
