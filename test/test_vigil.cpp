#include <crucible/Vigil.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

// Build a minimal TraceRing::Entry for testing.
static crucible::TraceRing::Entry make_entry(uint64_t schema_hash) {
    crucible::TraceRing::Entry e{};
    e.schema_hash      = schema_hash;
    e.shape_hash       = 0x1234;
    e.num_inputs       = 1;
    e.num_outputs      = 1;
    e.num_scalar_args  = 0;
    e.grad_enabled     = false;
    e.inference_mode   = false;
    return e;
}

// Build a minimal TensorMeta for one tensor (CPU, 1D float[8]).
static crucible::TensorMeta make_meta() {
    crucible::TensorMeta m{};
    m.ndim        = 1;
    m.sizes[0]    = 8;
    m.strides[0]  = 1;
    m.dtype       = crucible::ScalarType::Float;
    m.device_type = crucible::DeviceType::CPU;
    m.device_idx  = -1;
    m.layout      = crucible::Layout::Strided;
    m.data_ptr    = nullptr;
    return m;
}

int main() {
    // ── Create temp directory for Cipher ─────────────────────────────
    char tmpdir[] = "/tmp/crucible_vigil_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr);

    // ── Construct Vigil with persistence enabled ─────────────────────
    crucible::Vigil::Config cfg;
    cfg.cipher_path = dir;
    crucible::Vigil vigil(std::move(cfg));

    assert(!vigil.is_compiled());
    assert(vigil.current_step() == 0);
    assert(vigil.active_region() == nullptr);

    // ── Feed 15 ops (3 × 5 identical schema hashes) ──────────────────
    //
    // IterationDetector (K=5) fires after:
    //   Ops  1-5:  builds signature (NOT a match, just records first K ops)
    //   Ops  6-10: first match → candidate state (confirmed = true)
    //   Ops 11-15: second match → confirmed boundary → RegionNode created
    //
    // Each op carries 2 TensorMeta (1 input + 1 output) so build_trace()
    // can reconstruct a valid TraceGraph without a MetaLog miss.

    const uint64_t schemas[5] = {0xAA01, 0xBB02, 0xCC03, 0xDD04, 0xEE05};
    const crucible::TensorMeta meta = make_meta();
    const crucible::TensorMeta io_metas[2] = {meta, meta}; // [0]=input, [1]=output

    for (int iter = 0; iter < 3; iter++) {
        for (int j = 0; j < 5; j++) {
            auto e = make_entry(schemas[j]);
            const bool ok = vigil.record_op(e, io_metas, 2);
            assert(ok && "record_op must succeed (ring not full)");
        }
    }

    // ── flush() + wait for COMPILED mode (≤100ms) ────────────────────
    vigil.flush();

    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(100);
    while (!vigil.is_compiled()) {
        assert(steady_clock::now() < deadline
               && "Vigil did not reach COMPILED mode within 100ms");
        std::this_thread::sleep_for(milliseconds(1));
    }

    assert(vigil.is_compiled());
    assert(vigil.active_region() != nullptr);
    assert(vigil.current_step() >= 1);

    // ── persist() → Cipher HEAD must be non-zero ─────────────────────
    const bool persisted = vigil.persist();
    assert(persisted && "persist() must succeed with a cipher_path set");
    assert(vigil.head_hash() != 0 && "Cipher HEAD must be non-zero after persist()");

    // Verify the HEAD file was actually written to disk.
    std::ifstream hf(std::string(dir) + "/HEAD");
    assert(hf.is_open() && "HEAD file must exist on disk after persist()");
    std::string head_hex;
    std::getline(hf, head_hex);
    assert(!head_hex.empty() && "HEAD file must be non-empty");

    // ── replay() must call RegionExec exactly once ────────────────────
    int region_exec_count = 0;
    const crucible::RegionNode* exec_region_ptr = nullptr;

    const bool replayed = vigil.replay(
        // GuardEval: no guards in this simple linear region.
        [](const crucible::Guard&) -> int64_t { return 0; },
        // RegionExec: count calls and capture the pointer.
        [&region_exec_count, &exec_region_ptr](const crucible::RegionNode* r) {
            region_exec_count++;
            exec_region_ptr = r;
        });

    assert(replayed && "replay() must return true for a linear region");
    assert(region_exec_count == 1
           && "RegionExec must be called exactly once for a single-region DAG");
    assert(exec_region_ptr == vigil.active_region()
           && "replay() must execute the active region");

    // ── Cleanup ───────────────────────────────────────────────────────
    // Vigil destructor stops the background thread cleanly.
    // (vigil goes out of scope at end of block → stop() called)

    std::filesystem::remove_all(dir);

    std::printf("test_vigil: all tests passed\n");
    return 0;
}
