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
#include <fixy/check/Check.h>
#include <fixy/os/Process.h>
#include "test_harness.h"
#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

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
    ::fixy::check::pass(kTest, "  store_that_does_not_open_gives_no_store: PASSED\n");
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
    cfg.cipher_path.with_view([&ctx](::fixy::TextView path) {
        crucible::Cipher store =
            crucible::Cipher::open(ctx, ctx.cap().alloc, ::fixy::mint_tagged<::fixy::tags::source::External>(path))
                .expect("the test opens its store");
        const auto open_view = store.mint_open_view(ctx);
        store.record_event(ctx, open_view, crucible::ContentHash{0x0123456789ABCDEFULL}, 1)
            .expect("the store commits a head that names no object");
    });
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
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0));
    CRUCIBLE_TEST_ASSERT(
        kTest,
        ::fixy::find(report.output(), ::fixy::TextView{"[crucible] the Cipher store gives no region of its head: "})
            .is_some());
    CRUCIBLE_TEST_ASSERT(
        kTest,
        ::fixy::find(report.output(), crucible::load_error_text(crucible::LoadError::file_open_failed)).is_some());
    ::fixy::check::pass(kTest, "  refused_load_reports_its_reason: PASSED\n");
}

// The store of an earlier run holds the commits of the steps 1 to 10.  A new
// Vigil on that store starts its step after the last commit, so its persist
// commits at a later step.  The persist runs in a child, so a persist that
// ends its process shows as a child that does not exit with 0.  The old Vigil
// started at the step 0, and its first persist ended the process at the
// order check of the store.
static void test_store_of_an_earlier_run_takes_a_later_step(::fixy::TextView directory) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    crucible::Vigil::Config cfg;
    for (const char character : directory) {
        cfg.cipher_path.append(ctx.cap().alloc, character).expect("the store path fits the bound of the path");
    }
    for (const char character : ::fixy::TextView{"/earlier_run"}) {
        cfg.cipher_path.append(ctx.cap().alloc, character).expect("the store path fits the bound of the path");
    }
    cfg.cipher_path.with_view([&ctx](::fixy::TextView path) {
        crucible::Cipher store =
            crucible::Cipher::open(ctx, ctx.cap().alloc, ::fixy::mint_tagged<::fixy::tags::source::External>(path))
                .expect("the test opens its store");
        const auto open_view = store.mint_open_view(ctx);
        for (std::uint64_t step = 1; step <= 10; ++step) {
            store.record_event(ctx, open_view, crucible::ContentHash{0x5A00000000000000ULL + step}, step)
                .expect("the earlier run commits its step");
        }
    });
    crucible::Vigil vigil(std::move(cfg));
    if (!vigil.has_store()) ::fixy::fatal("the store of the earlier run does not open");
    divrig::feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(kTest, vigil);
    const ::fixy::process::ChildReport report =
        ::fixy::process::wait(::fixy::process::spawn_child(ctx,
                                                           [&vigil, &ctx] noexcept {
                                                               if (!vigil.persist(ctx)) {
                                                                   ::fixy::fatal("the persist of the new run fails");
                                                               }
                                                           })
                                  .expect("the test starts a child"))
            .expect("the test waits for its child");
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0) && "a persist to the store of an earlier run commits");
    CRUCIBLE_TEST_ASSERT(kTest, vigil.current_step() > 10 && "the new run starts after the last committed step");
    ::fixy::check::pass(kTest, "  store_of_an_earlier_run_takes_a_later_step: PASSED\n");
}

// A Vigil hashes each region that it publishes under the numerics policy and
// the eager word of its Config.  Two Vigils that record the same ops under two
// policies publish two content hashes.
static void test_region_hashes_under_the_numerics_of_the_config() {
    crucible::Vigil::Config strict_config;
    strict_config.numerics.determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
    strict_config.eager.bits = crucible::EagerNumerics::FLUSH_DENORMAL;
    crucible::Vigil strict{std::move(strict_config)};
    crucible::Vigil ordered;
    divrig::feed_one_region(strict, 0);
    divrig::feed_one_region(ordered, 0);
    crucible::test::flush_and_wait_region_published(kTest, strict);
    crucible::test::flush_and_wait_region_published(kTest, ordered);

    const ::fixy::Ref<const crucible::RegionNode> strict_region =
        strict.active_region().expect("the flush published a region");
    const ::fixy::Ref<const crucible::RegionNode> ordered_region =
        ordered.active_region().expect("the flush published a region");
    crucible::NumericsPolicy strict_policy{};
    strict_policy.determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
    CRUCIBLE_TEST_ASSERT(
        kTest, strict_region->numerics
                   == crucible::compute_numerics_hash(
                       strict_policy, crucible::EagerNumerics{.bits = crucible::EagerNumerics::FLUSH_DENORMAL}));
    CRUCIBLE_TEST_ASSERT(kTest,
                         ordered_region->numerics
                             == crucible::compute_numerics_hash(crucible::NumericsPolicy{}, crucible::EagerNumerics{}));
    CRUCIBLE_TEST_ASSERT(kTest, strict_region->content_hash != ordered_region->content_hash
                                    && "the numerics of the run separate two regions of the same ops");
    ::fixy::check::pass(kTest, "  region_hashes_under_the_numerics_of_the_config: PASSED\n");
}

// The Config of a Vigil whose store is the directory numerics under directory,
// under the numerics policy.
[[nodiscard]] static crucible::Vigil::Config config_with_store(::fixy::TextView directory,
                                                               crucible::NumericsPolicy policy) {
    crucible::Vigil::Config cfg;
    for (const char character : directory) {
        cfg.cipher_path.append(::foundation::effects::testing::test().alloc, character)
            .expect("the store path fits the bound of the path");
    }
    for (const char character : ::fixy::TextView{"/numerics"}) {
        cfg.cipher_path.append(::foundation::effects::testing::test().alloc, character)
            .expect("the store path fits the bound of the path");
    }
    cfg.numerics = policy;
    return cfg;
}

// A region replays only under the numerics that its content hash folds.  A
// Vigil under the default policy replays a region with a memory plan and
// stores it.  A Vigil under another policy loads that region and records, and
// a Vigil under the default policy loads it and replays it.  The old doors of
// the replay read no numerics, so the second Vigil replayed the region.
static void test_region_of_other_numerics_does_not_replay(::fixy::TextView directory) {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    {
        crucible::Vigil recorder{config_with_store(directory, crucible::NumericsPolicy{})};
        divrig::feed_one_region(recorder, 0);
        crucible::test::flush_and_wait_region_published(kTest, recorder);
        for (uint32_t i = 0; i < divrig::NUM_OPS; i++) {
            auto aligned = divrig::op_at(3, i);
            (void)crucible::test::dispatch_synthetic(recorder, aligned.entry, aligned.metas, aligned.n_metas);
        }
        CRUCIBLE_TEST_ASSERT(kTest, recorder.context().is_compiled() && "the recorder replays its region");
        CRUCIBLE_TEST_ASSERT(kTest, recorder.persist(ctx) && "the recorder stores its region");
    }

    crucible::NumericsPolicy strict{};
    strict.determinism = crucible::ReductionDeterminism::BITEXACT_STRICT;
    crucible::Vigil other{config_with_store(directory, strict)};
    CRUCIBLE_TEST_ASSERT(kTest, other.load(ctx, ctx.cap().alloc) && "the store gives the region of its head");
    CRUCIBLE_TEST_ASSERT(kTest, !other.context().is_compiled() && "a region of other numerics must not replay");
    CRUCIBLE_TEST_ASSERT(kTest, !other.is_compiled());

    crucible::Vigil same{config_with_store(directory, crucible::NumericsPolicy{})};
    CRUCIBLE_TEST_ASSERT(kTest, same.load(ctx, ctx.cap().alloc) && "the store gives the region of its head");
    CRUCIBLE_TEST_ASSERT(kTest, same.context().is_compiled() && "the region replays under its own numerics");
    ::fixy::check::pass(kTest, "  region_of_other_numerics_does_not_replay: PASSED\n");
}

// Repeated loads of one head keep only the regions that the foreground can
// still read.  Each load puts its region in an arena of its own, and the
// next load ends each arena whose region no holder names.  The holders name
// three loaded regions: the first, which the region cache keeps for its
// content hash, the one before the last, which the replay held at the last
// load, and the last.  The old Vigil put each loaded region in one arena that
// never ended its bytes.  The store of
// test_region_of_other_numerics_does_not_replay holds the head.
static void test_repeated_loads_keep_a_bounded_heap(::fixy::TextView directory) {
    constexpr uint32_t kLoads = 2000;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    crucible::Vigil loader{config_with_store(directory, crucible::NumericsPolicy{})};
    const crucible::VigilFgCtx fg = loader.mint_producer_context();
    for (uint32_t load = 0; load < kLoads; ++load) {
        CRUCIBLE_TEST_ASSERT(ctx, loader.load(ctx, ctx.cap().alloc));
        CRUCIBLE_TEST_ASSERT(ctx, loader.kept_load_count(fg) <= 3);
    }
    CRUCIBLE_TEST_ASSERT(ctx, loader.context().is_compiled());
    CRUCIBLE_TEST_ASSERT(ctx, loader.kept_load_count(fg) == 3);
    ::fixy::check::pass(kTest, "  repeated_loads_keep_a_bounded_heap: PASSED\n");
}

// A change of the eager word stops the replay of the region of the old word,
// counts one divergence, and the pipeline cuts the next region under the new
// word, which replays.  The dispatch that activates a region and the
// dispatch that completes it report a region entry, and no other dispatch
// does.  The old Vigil had no door for the eager word after the attach, so
// the region of the old word kept on replaying.
static void test_eager_word_change_is_a_divergence() {
    crucible::Vigil vigil;
    const crucible::VigilFgCtx fg = vigil.mint_producer_context();
    divrig::feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(kTest, vigil);
    uint32_t entries = 0;
    for (uint32_t i = 0; i < divrig::NUM_OPS; i++) {
        auto aligned = divrig::op_at(3, i);
        const crucible::DispatchResult result =
            crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
        if (result.enters_region) {
            ++entries;
            CRUCIBLE_TEST_ASSERT(kTest, (i + 1 == divrig::K || i + 1 == divrig::NUM_OPS)
                                            && "only the activation and the completion of the region report an entry");
        }
    }
    CRUCIBLE_TEST_ASSERT(kTest, entries == 2 && "the activation and the completion each report a region entry");
    CRUCIBLE_TEST_ASSERT(kTest,
                         vigil.context().is_compiled() && "the foreground replays the region of the attach word");
    const crucible::ContentHash attach_hash =
        vigil.active_region().expect("the flush published a region")->content_hash;
    const uint64_t diverged = vigil.diverged_count();

    vigil.observe_eager_numerics(fg, crucible::EagerNumerics{});
    CRUCIBLE_TEST_ASSERT(kTest, vigil.context().is_compiled() && vigil.diverged_count() == diverged
                                    && "the same word changes nothing");

    const crucible::EagerNumerics flushing{.bits = crucible::EagerNumerics::FLUSH_DENORMAL};
    vigil.observe_eager_numerics(fg, flushing);
    CRUCIBLE_TEST_ASSERT(kTest,
                         !vigil.context().is_compiled() && !vigil.is_compiled() && "a changed word stops the replay");
    CRUCIBLE_TEST_ASSERT(kTest, vigil.diverged_count() == diverged + 1 && "a changed word counts one divergence");
    CRUCIBLE_TEST_ASSERT(kTest, vigil.eager_numerics() == flushing);

    divrig::feed_one_region(vigil, 4);
    crucible::test::flush_and_wait_region_published(kTest, vigil);
    const ::fixy::Ref<const crucible::RegionNode> flushed =
        vigil.active_region().expect("the pipeline published a region after the change");
    CRUCIBLE_TEST_ASSERT(kTest,
                         flushed->numerics == crucible::compute_numerics_hash(crucible::NumericsPolicy{}, flushing)
                             && "the region after the change holds the hash of the new word");
    CRUCIBLE_TEST_ASSERT(kTest,
                         flushed->content_hash != attach_hash && "the new word gives the region a new content hash");
    for (uint32_t i = 0; i < divrig::NUM_OPS; i++) {
        auto aligned = divrig::op_at(7, i);
        (void)crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
    }
    CRUCIBLE_TEST_ASSERT(kTest, vigil.context().is_compiled() && "the region of the new word replays");
    CRUCIBLE_TEST_ASSERT(kTest, vigil.diverged_count() == diverged + 1);
    ::fixy::check::pass(kTest, "  eager_word_change_is_a_divergence: PASSED\n");
}

int main() {
    test_region_hashes_under_the_numerics_of_the_config();
    test_eager_word_change_is_a_divergence();
    test_vigil::test_divergence_drops_the_unobserved_region();
    test_vigil::test_divergence_drops_the_regions_of_the_backlog();
    test_vigil::test_divergence_drops_a_half_finished_alignment();
    test_vigil::test_rollback_drops_the_region_in_the_slot();
    test_vigil::test_rollback_closes_an_open_alignment();
    test_vigil::test_region_store_stays_bounded();
    test_vigil::test_small_region_store_discards_and_counts();
    test_vigil::test_divergence_leaves_no_stale_active_region();
    test_vigil::test_same_region_keeps_the_alignment();
    test_vigil::test_record_op_claims_the_producer_role();
    test_vigil::test_producer_surface_takes_the_claim_context();
    test_vigil::test_two_publishes_keep_one_copy_of_the_head();
    test_vigil::test_foreground_reads_the_iteration_count();
    test_vigil::test_region_keeps_its_metadata_after_the_log_wraps();
    test_vigil::test_region_keeps_the_depth_of_each_op();
    test_vigil::test_region_keeps_the_depth_past_a_growth_of_the_held_ops();
    test_vigil::test_op_nest_counts_the_open_records();
    test_vigil::test_unrecorded_nest_gives_its_closing_entry();
    test_vigil::test_unrecorded_nest_keeps_the_loop_eager();
    test_vigil::test_replay_diverges_at_the_close_of_a_nest();
    test_vigil::test_raise_in_each_iteration_after_a_replay_keeps_the_loop_eager();
    test_vigil::test_external_follows_the_live_op();
    test_vigil::test_views_of_an_external_bind_its_storage();
    test_vigil::test_internal_slot_of_a_nest_keeps_its_pool_bytes();
    test_store_that_does_not_open_gives_no_store();

    char tmpdir[] = "/tmp/crucible_vigil_XXXXXX";
    char* dir = mkdtemp(tmpdir);
    CRUCIBLE_TEST_ASSERT(kTest, dir != nullptr);

    // The text of the path copies each character of the directory.
    crucible::Vigil::Config cfg;
    for (std::size_t index = 0; dir[index] != '\0'; ++index) {
        cfg.cipher_path.append(::foundation::effects::testing::test().alloc, dir[index])
            .expect("the directory fits the bound of the path");
    }
    cfg.cipher_path.with_view([](::fixy::TextView path) {
        test_refused_load_reports_its_reason(path);
        test_store_of_an_earlier_run_takes_a_later_step(path);
        test_region_of_other_numerics_does_not_replay(path);
        test_repeated_loads_keep_a_bounded_heap(path);
    });
    crucible::Vigil vigil(std::move(cfg));

    CRUCIBLE_TEST_ASSERT(kTest, !vigil.is_compiled());
    CRUCIBLE_TEST_ASSERT(kTest, vigil.current_step() == 0);
    CRUCIBLE_TEST_ASSERT(kTest, vigil.active_region().is_none());

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
            CRUCIBLE_TEST_ASSERT(kTest, ok && "record_op must succeed (ring not full)");
        }
    }

    crucible::test::flush_and_wait_region_published(kTest, vigil);

    // A published region is not a replay. The mode turns COMPILED only when
    // the foreground aligns to the region and activates the context, and
    // this test dispatches nothing after the flush.
    CRUCIBLE_TEST_ASSERT(kTest, vigil.has_pending_region());
    CRUCIBLE_TEST_ASSERT(kTest, !vigil.is_compiled() && "publication alone must not report a replay");
    CRUCIBLE_TEST_ASSERT(kTest, !vigil.context().is_compiled());
    CRUCIBLE_TEST_ASSERT(kTest, vigil.active_region().is_some());
    CRUCIBLE_TEST_ASSERT(kTest, vigil.current_step() >= 1);
    const ::fixy::Ref<const crucible::RegionNode> active_region =
        vigil.active_region().expect("the flush published a region");
    const auto object_path = object_path_for(dir, active_region->content_hash);
    CRUCIBLE_TEST_ASSERT(kTest, !std::filesystem::exists(object_path)
                                    && "background Vigil callback must not pre-store Cipher objects");
    CRUCIBLE_TEST_ASSERT(kTest, !std::filesystem::exists(std::string(dir) + "/HEAD")
                                    && "background Vigil callback must not advance Cipher HEAD");

    // The store writes and flushes files, so persist takes a context whose row
    // admits IO and Block.
    const bool persisted = vigil.persist(::fixy::TestRunnerCtx{::foundation::effects::testing::test()});
    CRUCIBLE_TEST_ASSERT(kTest, persisted && "persist() must succeed with a cipher_path set");
    CRUCIBLE_TEST_ASSERT(kTest, static_cast<bool>(vigil.head_hash()) && "Cipher HEAD must be non-zero after persist()");
    CRUCIBLE_TEST_ASSERT(kTest, std::filesystem::exists(object_path)
                                    && "foreground persist() must be the direct Cipher object writer");

    std::ifstream hf(std::string(dir) + "/HEAD");
    CRUCIBLE_TEST_ASSERT(kTest, hf.is_open() && "HEAD file must exist on disk after persist()");
    std::string head_hex;
    std::getline(hf, head_hex);
    CRUCIBLE_TEST_ASSERT(kTest, !head_hex.empty() && "HEAD file must be non-empty");

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

    CRUCIBLE_TEST_ASSERT(kTest, replayed && "replay() must return true for a linear region");
    CRUCIBLE_TEST_ASSERT(kTest,
                         region_exec_count == 1 && "RegionExec must be called exactly once for a single-region DAG");
    CRUCIBLE_TEST_ASSERT(kTest, exec_region_ptr == &*vigil.active_region().expect("the flush published a region")
                                    && "replay() must execute the active region");

    // The stored region has no memory plan: its tensors have no data.  A load
    // of it while the context replays a different region stops that replay,
    // because the loaded region is the active region and cannot replay.
    const crucible::ContentHash stored_hash = active_region->content_hash;
    CRUCIBLE_TEST_ASSERT(kTest, active_region->plan.is_none() && "the stored region must have no memory plan");
    divrig::feed_one_region(vigil, 0);
    crucible::test::flush_and_wait_region_published(kTest, vigil);
    for (uint32_t i = 0; i < divrig::NUM_OPS; i++) {
        auto aligned = divrig::op_at(3, i);
        (void)crucible::test::dispatch_synthetic(vigil, aligned.entry, aligned.metas, aligned.n_metas);
    }
    CRUCIBLE_TEST_ASSERT(kTest,
                         vigil.context().is_compiled() && "the foreground must align on the region with tensors");

    CRUCIBLE_TEST_ASSERT(kTest, vigil.load(::fixy::TestRunnerCtx{::foundation::effects::testing::test()},
                                           ::foundation::effects::testing::test().alloc)
                                    && "load() must find the stored head");
    CRUCIBLE_TEST_ASSERT(kTest,
                         vigil.active_region().expect("the load made a region active")->content_hash == stored_hash
                             && "load() must make the stored region active");
    CRUCIBLE_TEST_ASSERT(kTest, !vigil.context().is_compiled()
                                    && "the context replays a region that is not the active region");
    CRUCIBLE_TEST_ASSERT(kTest,
                         !vigil.is_compiled() && "the mode reports a replay of a region that is not the active region");

    std::filesystem::remove_all(dir);

    ::fixy::check::pass(kTest, "test_vigil: all tests passed\n");
    return 0;
}
