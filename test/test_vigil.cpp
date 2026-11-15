// The test is several source files of one executable, so that no
// translation unit holds every test:
//
//   vigil_rig.h                   the shared part
//   this file                     the persistence test and main
//   ..._divergence.cpp            the rig, and a divergence that drops what
//                                 the foreground has not observed
//   ..._producer.cpp              the producer role and its surface
//   ..._metadata.cpp              a region keeps its tensor metadata after
//                                 the foreground writes over the log

#include "vigil_rig.h"

#include <crucible/Cipher.h>
#include <crucible/Vigil.h>
#include <fixy/os/Process.h>
#include "test_harness.h"
#include "test_assert.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

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

// A store path under /dev/null, which is no directory, gives a Vigil with no
// store.  The Vigil reports the reason, and persist gives false.
static void test_store_that_does_not_open_gives_no_store() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    crucible::Vigil::Config cfg;
    for (const char character : ::fixy::TextView{"/dev/null/store"}) {
        cfg.cipher_path.append(ctx.cap().alloc, character).expect("the store path fits the bound of the path");
    }
    crucible::Vigil vigil(std::move(cfg));
    if (vigil.has_store() || static_cast<bool>(vigil.head_hash()) || vigil.persist(ctx)) {
        ::fixy::fatal("a store path under /dev/null gave a Vigil with a store");
    }
    crucible::test::pass("  store_that_does_not_open_gives_no_store: PASSED\n");
}

// The head of a store names an object that the store does not hold, so a
// load gives no region, and the Vigil reports the reason.  A Cipher of the
// test commits that head under directory.  The load runs in a child, so the
// test reads the report.  The child only loads: it reads files, allocates
// and writes the report, and it starts no thread and waits for none.
static void test_refused_load_reports_its_reason(::fixy::TextView directory) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    crucible::Vigil::Config cfg;
    for (const char character : directory) {
        cfg.cipher_path.append(ctx.cap().alloc, character).expect("the store path fits the bound of the path");
    }
    for (const char character : ::fixy::TextView{"/refused"}) {
        cfg.cipher_path.append(ctx.cap().alloc, character).expect("the store path fits the bound of the path");
    }
    {
        crucible::Cipher store =
            crucible::Cipher::open(ctx, ctx.cap().alloc,
                                   ::fixy::mint_tagged<::fixy::tags::source::External>(cfg.cipher_path.view()))
                .expect("the test opens its store");
        const auto open_view = store.mint_open_view(ctx);
        store.record_event(ctx, open_view, crucible::ContentHash{0x0123456789ABCDEFULL}, 1)
            .expect("the store commits a head that names no object");
    }
    crucible::Vigil vigil(std::move(cfg));
    if (!vigil.has_store()) ::fixy::fatal("the store of the test does not open");
    const ::fixy::process::ChildReport report =
        ::fixy::process::wait(::fixy::process::spawn_child(ctx,
                                                           [&vigil, &ctx] noexcept {
                                                               if (vigil.load(ctx, ctx.cap().alloc)) {
                                                                   ::fixy::fatal("a head with no object gave a region");
                                                               }
                                                           })
                                  .expect("the test starts a child"))
            .expect("the test waits for its child");
    assert(report.is_exit_with(0));
    assert(::fixy::find(report.output(), ::fixy::TextView{"[crucible] the Cipher store gives no region of its head: "})
               .is_some());
    assert(::fixy::find(report.output(), crucible::load_error_text(crucible::LoadError::file_open_failed)).is_some());
    crucible::test::pass("  refused_load_reports_its_reason: PASSED\n");
}

int main() {
    test_vigil::test_divergence_drops_the_unobserved_region();
    test_vigil::test_divergence_drops_the_regions_of_the_backlog();
    test_vigil::test_divergence_drops_a_half_finished_alignment();
    test_vigil::test_rollback_drops_the_region_in_the_slot();
    test_vigil::test_rollback_closes_an_open_alignment();
    test_vigil::test_region_store_stays_bounded();
    test_vigil::test_divergence_leaves_no_stale_active_region();
    test_vigil::test_same_region_keeps_the_alignment();
    test_vigil::test_record_op_claims_the_producer_role();
    test_vigil::test_producer_surface_takes_the_claim_context();
    test_vigil::test_foreground_reads_the_iteration_count();
    test_vigil::test_region_keeps_its_metadata_after_the_log_wraps();
    test_store_that_does_not_open_gives_no_store();

    char tmpdir[] = "/tmp/crucible_vigil_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    assert(dir != nullptr);

    // The text of the path copies each character of the directory.
    crucible::Vigil::Config cfg;
    for (std::size_t index = 0; dir[index] != '\0'; ++index) {
        cfg.cipher_path.append(::foundation::effects::testing::test().alloc, dir[index])
            .expect("the directory fits the bound of the path");
    }
    test_refused_load_reports_its_reason(cfg.cipher_path.view());
    crucible::Vigil vigil(std::move(cfg));

    assert(!vigil.is_compiled());
    assert(vigil.current_step() == 0);
    assert(vigil.active_region().is_none());

    // Fifteen ops, as three repeats of the same five schema hashes. The
    // detector needs a signature of five, then a first match, then a second
    // match before it confirms a boundary, so three repeats is the minimum
    // that produces a region. Each op carries two tensor metas, one in and
    // one out, or the trace cannot be rebuilt.

    const SchemaHash schemas[5] = {SchemaHash{0xAA01}, SchemaHash{0xBB02}, SchemaHash{0xCC03}, SchemaHash{0xDD04},
                                   SchemaHash{0xEE05}};
    const crucible::TensorMeta meta = make_meta();
    const ::fixy::FixedArray<crucible::TensorMeta, 2> io_metas{meta, meta};  // [0]=input, [1]=output

    for (int iter = 0; iter < 3; iter++) {
        for (int j = 0; j < 5; j++) {
            auto e = make_entry(schemas[j]);
            const bool ok = vigil.record_op(crucible::test::certify_synthetic_op(e, io_metas, 2));
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
    assert(vigil.active_region().is_some());
    assert(vigil.current_step() >= 1);
    const ::fixy::Ref<const crucible::RegionNode> active_region =
        vigil.active_region().expect("the flush published a region");
    const auto object_path = object_path_for(dir, active_region->content_hash);
    assert(!std::filesystem::exists(object_path) && "background Vigil callback must not pre-store Cipher objects");
    assert(!std::filesystem::exists(std::string(dir) + "/HEAD")
           && "background Vigil callback must not advance Cipher HEAD");

    // The store writes and flushes files, so persist takes a context whose row
    // admits IO and Block.
    const bool persisted = vigil.persist(::fixy::TestRunnerCtx{::foundation::effects::testing::test()});
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
        [&region_exec_count, &exec_region_ptr](const crucible::RegionNode& r) {
            region_exec_count++;
            exec_region_ptr = &r;
        });

    assert(replayed && "replay() must return true for a linear region");
    assert(region_exec_count == 1 && "RegionExec must be called exactly once for a single-region DAG");
    assert(exec_region_ptr == &*vigil.active_region().expect("the flush published a region")
           && "replay() must execute the active region");

    // The stored region has no memory plan: its tensors have no data.  A load
    // of it while the context replays a different region stops that replay,
    // because the loaded region is the active region and cannot replay.
    const crucible::ContentHash stored_hash = active_region->content_hash;
    assert(active_region->plan.is_none() && "the stored region must have no memory plan");
    divrig::feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(vigil);
    for (uint32_t i = 0; i < divrig::NUM_OPS; i++) {
        auto aligned = divrig::op_at(3, i);
        (void)crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
    }
    assert(vigil.context().is_compiled() && "the foreground must align on the region with tensors");

    assert(vigil.load(::fixy::TestRunnerCtx{::foundation::effects::testing::test()},
                      ::foundation::effects::testing::test().alloc)
           && "load() must find the stored head");
    assert(vigil.active_region().expect("the load made a region active")->content_hash == stored_hash
           && "load() must make the stored region active");
    assert(!vigil.context().is_compiled() && "the context replays a region that is not the active region");
    assert(!vigil.is_compiled() && "the mode reports a replay of a region that is not the active region");

    std::filesystem::remove_all(dir);

    crucible::test::pass("test_vigil: all tests passed\n");
    return 0;
}
