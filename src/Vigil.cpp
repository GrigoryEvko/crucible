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
#include <fixy/Core.h>

#include <utility>

namespace crucible {

static_assert(Vigil::Config::CIPHER_PATH_MAX == Cipher::MAX_ROOT_PATH_BYTES,
              "the bound of Vigil::Config::cipher_path must be the bound of the root of the store");

Vigil::Vigil(Config cfg, ::fixy::InitLoadCtx const& startup)
    : cfg_(std::move(cfg)),
      ring_owner_{::fixy::mint_box<TraceRing>(startup.cap().alloc)},
      ring_{ring_owner_.view()},
      meta_log_owner_{::fixy::mint_box<MetaLog>(startup.cap().alloc, startup.cap().alloc)},
      meta_log_{meta_log_owner_.view()},
      tx_log_{startup},
      replay_{startup.cap().alloc, compute_numerics_hash(cfg_.numerics, cfg_.eager)},
      allocation_{startup.cap().alloc},
      region_ready_{BackgroundThread::RegionReadyCall::bind<&Vigil::on_region_ready>(*this)},
      bg_{startup.cap().alloc, region_ready_} {
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
            ::crucible::warden::mint_deadline_watchdog(startup, ::crucible::warden::WatchdogSource::some(senses.view()),
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

bool Vigil::rollback(::foundation::effects::cap::Nondet nondet) {
    const VigilFgCtx fg = assert_producer_thread_();
    // No op reaches the ring while this thread rolls back, so this is the
    // point of the rollback.
    const uint64_t ops_before = ring_total_produced();
    bool restored_one = false;
    ::fixy::WeakRef<RegionNode> restored{};
    bg_.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        restored_one = tx_log_.rollback(stage, nondet);
        if (!restored_one) return;
        // The job runs between two publications, so each publication after
        // it reads the mark, and none gives the foreground a region cut
        // before the rollback.
        bg_.request_mark(ops_before);
        const ::fixy::WeakRef<Transaction> active = tx_log_.active(stage);
        if (!active.has_value()) return;
        for (const ::fixy::Ref<RegionNode>& region : active->region.value()) {
            restored = ::fixy::WeakRef<RegionNode>{*region};
            // The foreground gets the region outside the publication slot,
            // so the store keeps it until the foreground writes its holders
            // again.
            bg_.keep_region(stage, ::fixy::WeakRef<const RegionNode>{*region});
        }
    });
    if (!restored_one) return false;
    // The restored transaction was active before, and only a commit with a
    // region makes a transaction active.
    CRUCIBLE_FATAL_INVARIANT(restored.has_value());
    adopt_region_(fg, restored.get());
    return true;
}

void Vigil::adopt_region_(VigilFgCtx const& fg, RegionNode& region) {
    // No op reached the ring after the mark, so the pipeline cut each region
    // that the slot can hold before the mark.
    cache_pending_region_();
    pending_activation_.reset();
    alignment_pos_ = alignment_pos_at(0);
    alignment_ops_ = 0;
    taken_region_.store_release(::fixy::WeakRef<RegionNode>{region});
    if (replay_.activate(region)) {
        // A rollback or a load resumes the replay at the first op.
        register_externals_from_region_(fg, region, 0);
        region_cache_.insert(region);
    }
    publish_held_regions_();
}

void Vigil::on_region_ready(::foundation::effects::Bg const& bg, BackgroundThread::PublishStage const& stage,
                            BackgroundThread::PublishedRegion const& published) noexcept {
    RegionNode& region = published.region.get();
    // bump returns the previous value, which is the index this call
    // reserved.  The background thread is the sole writer.
    const uint64_t step = step_.bump();

    Transaction& tx = tx_log_.begin_tx(stage, bg.nondet, step);
    // The result is discarded because a state-machine logic error here
    // is not something the background thread can recover from.
    //
    // The merkle root goes through the checked accessor so the non-zero
    // invariant is witnessed at this call site: its precondition fires
    // here if the hash was never recomputed.
    (void)tx_log_.commit(stage, bg.nondet, tx,
                         ::fixy::mint_tagged<::fixy::tags::source::Arena>(::fixy::Ref<RegionNode>{::fixy::to(region)}),
                         region.content_hash, ::crucible::make_merkle_root(region.computed_merkle_hash()));
    (void)tx_log_.activate(stage, bg.nondet, tx);

    // Publishing the region signals the foreground, which picks it up
    // on its next dispatch.  The mode stays RECORDING here.  The
    // foreground still records until it aligns to this region, and it
    // publishes COMPILED when it activates the context, so is_compiled
    // never reports a replay that has not started.  An observer that
    // waits for this publication reads has_pending_region.  The slot holds
    // the record, so the foreground can compare the reset point.
    pending_region_.store_release(PendingRegion::some(::fixy::to(published)));

    // The sweep of the region store after this callback releases each region
    // that no holder names.  The transaction log holds each region that a
    // rollback can restore, the slot holds the region that the foreground
    // did not get, and the cells of held_regions_ hold the regions of the
    // foreground.
    BackgroundThread::RegionMarks marks{};
    tx_log_.for_each_region(stage, [&marks](::fixy::WeakRef<const RegionNode> held) noexcept { marks.hold(held); });
    const PendingRegion handed = pending_region_.load_acquire();
    for (const auto& record : handed)
        marks.handed = ::fixy::WeakRef<const RegionNode>{*record->region};
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
