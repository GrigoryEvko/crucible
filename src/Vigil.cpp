// The bodies of the cold members of crucible/Vigil.h: the constructor that
// starts the runtime, the destructor, the head of the store, the region-ready
// callback that runs on the publish stage, and the rollback.
//
// The hot path (record_op, dispatch_op and the replay guard) and the cold
// helpers that dispatch_op calls stay in the header, so a caller of
// dispatch_op compiles the same machine code as before.

#include <crucible/Vigil.h>

#include <crucible/Cipher.h>
#include <crucible/perf/Senses.h>
#include <crucible/warden/DeadlineWatchdog.h>
#include <fixy/handle/Published.h>
#include <fixy/Core.h>

#include <utility>

namespace crucible {

static_assert(Vigil::Config::CIPHER_PATH_MAX == Cipher::MAX_ROOT_PATH_BYTES,
              "the bound of Vigil::Config::cipher_path must be the bound of the root of the store");

Vigil::Vigil(Config cfg, ::fixy::InitLoadCtx const& startup)
    : cfg_(std::move(cfg)),
      ring_owner_{::fixy::mint_box<TraceRing>(startup.cap().alloc)},
      ring_{::fixy::erase_brand(ring_owner_.view())},
      meta_log_owner_{::fixy::mint_box<MetaLog>(startup.cap().alloc, startup.cap().alloc)},
      meta_log_{::fixy::erase_brand(meta_log_owner_.view())},
      tx_log_{},
      replay_{startup.cap().alloc, compute_numerics_hash(cfg_.numerics, cfg_.eager)},
      allocation_{startup.cap().alloc},
      region_ready_{BackgroundThread::RegionReadyCall::bind<&Vigil::on_region_ready>(*this)},
      bg_{startup.cap().alloc, region_ready_, cfg_.limits} {
    ring_->reset();
    meta_log_->reset();

    // An empty path gives no store.  The configured path is operator-supplied,
    // so it crosses the trust boundary here and is declared external until
    // the store's own sanitizer promotes it.  A root that does not open gives
    // no store, and the report tells the operator the reason.
    if (cfg_.cipher_path.size() != 0) {
        cfg_.cipher_path.with_view([&](::fixy::TextView path) {
            Cipher::open(startup, startup.cap().alloc, ::fixy::mint_tagged<::fixy::tags::source::External>(path))
                .match(
                    [&](Cipher opened) {
                        // A store of an earlier run holds its steps, and each
                        // commit of this run must come at or after them.  So
                        // the step starts after the last commit.  The
                        // background thread is the one writer of the step,
                        // and it starts below, so this write comes first.
                        for (const ::fixy::session::StepId last : opened.last_committed_step()) {
                            step_.advance(last.raw() + 1);
                        }
                        cipher_ = ::fixy::some(::fixy::mint_box<Cipher>(startup.cap().alloc, std::move(opened)));
                    },
                    [&](Cipher::OpenError reason) {
                        ::fixy::report(::fixy::Sink::Err, "[crucible] the Cipher store at {} does not open: {}\n", path,
                                       Cipher::open_error_text(reason));
                    });
        });
    }

    // The watchdog is constructed before the background thread starts,
    // so on_region_ready can observe on the very first region transition
    // without a null check.  Attach failure is not an error here: every
    // observation then returns InsufficientData.
    if (cfg_.enable_deadline_watchdog) {
        // A Box keeps the address of its object when it moves, so the
        // borrow of the watchdog stays valid after the move into senses_.
        ::fixy::Box<::crucible::perf::Senses> senses = ::fixy::mint_box<::crucible::perf::Senses>(
            startup.cap().alloc,
            ::crucible::perf::Senses::load_subset(startup, ::crucible::perf::SensesMask{.sched_switch = true}));
        wd_ = ::fixy::some(::fixy::mint_box<::crucible::warden::DeadlineWatchdog>(
            startup.cap().alloc,
            ::crucible::warden::mint_deadline_watchdog(
                startup, ::crucible::warden::WatchdogSource::some(::fixy::erase_brand(senses.view())),
                cfg_.watchdog_policy)));
        senses_ = ::fixy::some(std::move(senses));
    }

    eager_ = cfg_.eager;
    bg_.set_numerics(compute_numerics_hash(cfg_.numerics, cfg_.eager));
    bg_.start(startup, *ring_, *meta_log_, cfg_.rank, cfg_.world_size, cfg_.device_capability);
}

Vigil::~Vigil() = default;

ContentHash Vigil::head_hash() const noexcept {
    return cipher_.match([](::fixy::Box<Cipher> const& cipher) noexcept { return cipher->head(); },
                         []() noexcept { return ContentHash{}; });
}

bool Vigil::rollback() {
    const VigilFgCtx fg = assert_producer_thread_();
    // No op reaches the ring while this thread rolls back, so this is the
    // point of the rollback.
    const uint64_t ops_before = ring_total_produced();
    const ::fixy::Option<::fixy::handle::Published<RegionNode, DagArena>> restored =
        bg_.run_on_publish_stage([&tx_log = tx_log_, ops_before](BackgroundThread::PublishStageView& bg,
                                                                 BackgroundThread::PublishStage const& stage) noexcept
                                     -> ::fixy::Option<::fixy::handle::Published<RegionNode, DagArena>> {
            if (!tx_log.rollback(stage)) return ::fixy::none;
            // The job runs between two publications, so each publication after
            // it reads the mark, and none gives the foreground a region cut
            // before the rollback.
            bg.request_mark(ops_before);
            // The restored transaction was active before, and only a commit
            // with a region makes a transaction active.
            const auto active = tx_log.active(stage).expect("a rollback leaves the restored transaction active");
            ::fixy::Option<::fixy::handle::Published<RegionNode, DagArena>> region_of_rollback{};
            const bool has_region =
                tx_log
                    .with_tx(stage, active,
                             [&bg, &stage, &region_of_rollback](Transaction const& tx) noexcept {
                                 for (const DagRef<RegionNode const>& region : tx.region.value()) {
                                     region_of_rollback = ::fixy::some(::fixy::handle::mint_published(stage, region));
                                     // The foreground gets the region outside the publication slot,
                                     // so the store keeps it until the foreground writes its holders
                                     // again (the life of a region of BackgroundThread.h).
                                     bg.keep_region(stage, ::fixy::some(region));
                                 }
                                 return region_of_rollback.is_some();
                             })
                    .expect("no claim runs between the rollback and this read");
            CRUCIBLE_FATAL_INVARIANT(has_region);
            return region_of_rollback;
        });
    return restored.match(
        [this, &fg](::fixy::handle::Published<RegionNode, DagArena> const& region) {
            adopt_region_(fg, region.get());
            return true;
        },
        []() { return false; });
}

void Vigil::adopt_region_(VigilFgCtx const& fg, RegionNode const& region) {
    // No op reached the ring after the mark, so the pipeline cut each region
    // that the slot can hold before the mark.
    cache_pending_region_();
    pending_activation_ = ::fixy::none;
    alignment_pos_ = alignment_pos_at(0);
    alignment_ops_ = 0;
    taken_region_.store_release(::fixy::some(::fixy::to(region)));
    if (replay_.activate(region)) {
        // A rollback or a load resumes the replay at the first op.
        register_inputs_from_region_(fg, region, 0);
        region_cache_.insert(region);
    }
    publish_held_regions_();
}

void Vigil::on_region_ready(::foundation::effects::Bg const& bg, BackgroundThread::PublishStage const& stage,
                            BackgroundThread::PublishedRegion const& published) noexcept {
    RegionNode const& region =
        *ConstRegionLink{published.region}.expect("the publish stage gives a record of a region");
    // bump returns the previous value, which is the index this call
    // reserved.  The background thread is the sole writer.
    const ::fixy::session::StepId step{step_.bump()};

    // The draft is the one writer of the new transaction, and the commit
    // consumes it.  The merkle root goes through the checked accessor so the
    // non-zero invariant is witnessed at this call site: its precondition
    // fires here if the hash was never recomputed.
    const Transaction::ArenaRegion committed_region =
        ::fixy::mint_tagged<::fixy::tags::source::Arena>(DagRef<RegionNode const>{::fixy::to(region)});
    const MerkleHash merkle_root = ::crucible::make_merkle_root(region.computed_merkle_hash());
    const auto committed =
        tx_log_.commit(stage, tx_log_.begin_tx(stage, step), committed_region, region.content_hash, merkle_root)
            .expect("no claim runs between the claim of this call and its commit");
    // The activation of the transaction that this call committed finds it
    // committed and in its slot, so it gives no refusal.
    static_cast<void>(tx_log_.activate(stage, committed).expect("the transaction of this commit is fresh"));

    // Publishing the region signals the foreground, which picks it up
    // on its next dispatch.  The mode stays RECORDING here.  The
    // foreground still records until it aligns to this region, and it
    // publishes COMPILED when it activates the context, so is_compiled
    // never reports a replay that has not started.  An observer that
    // waits for this publication reads has_pending_region.  The slot holds
    // the record, so the foreground can compare the reset point.
    pending_region_.store_release(PendingRegion::some(::fixy::erase_brand(::fixy::to(published))));

    // The sweep of the region store after this callback releases each region
    // that no holder names.  The transaction log holds each region that a
    // rollback can restore, the slot holds the region that the foreground
    // did not get, and the cells of held_regions_ hold the regions of the
    // foreground.
    BackgroundThread::RegionMarks marks{};
    tx_log_.for_each_region(stage, [&marks](ConstRegionLink held) noexcept { marks.hold(held); });
    const PendingRegion handed = pending_region_.load_acquire();
    for (const auto& record : handed)
        marks.handed = record->region;
    mark_held_regions_(marks);
    bg_.set_region_marks(stage, marks);

    // The observation is presented with a background observe context
    // because this runs on the region-publishing thread, not the
    // foreground, and a watchdog reads a clock.  The frame door writes the
    // watchdog in place and writes no byte of wd_, which the foreground can
    // read.
    wd_.with_mut(
        [this, &bg](auto watchdog) noexcept {
            const auto v = (*watchdog)->observe(::fixy::BgObserveCtx{bg});
            wd_last_verdict_.store_release(v);
            switch (v) {
                case ::crucible::warden::WatchdogVerdict::Healthy:
                    (void)wd_healthy_count_.fetch_add_acq_rel(1);
                    break;
                case ::crucible::warden::WatchdogVerdict::Downgrade:
                    (void)wd_downgrade_count_.fetch_add_acq_rel(1);
                    break;
                case ::crucible::warden::WatchdogVerdict::InsufficientData:
                    (void)wd_insufficient_count_.fetch_add_acq_rel(1);
                    break;
                default:
                    // The verdict enum has exactly the three values above.  A
                    // different value ends the process in each build.
                    ::fixy::unreachable();
            }
        },
        [] noexcept {});
}

}  // namespace crucible
