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
#include <foundation/core/Grow.h>

#include <utility>

namespace crucible {

Vigil::Vigil(Config cfg, ::fixy::InitLoadCtx const& startup)
    : cfg_(std::move(cfg)),
      ring_owner_{::fixy::mint_box<TraceRing>(startup.cap().alloc)},
      ring_{ring_owner_.view()},
      meta_log_owner_{::fixy::mint_box<MetaLog>(startup.cap().alloc)},
      meta_log_{meta_log_owner_.view()},
      tx_log_{startup} {
    ring_->reset();
    meta_log_->reset();

    // The text of the root copies the characters of the configured path.  An
    // empty path, or a path longer than the store takes, gives no store.
    ::foundation::core::GrowText root_text = ::foundation::core::mint_grow_text(Cipher::MAX_ROOT_PATH_BYTES);
    bool is_root_copied = true;
    for (std::size_t index = 0; index < cfg_.cipher_path.size(); ++index) {
        is_root_copied = is_root_copied && root_text.append(startup.cap().alloc, cfg_.cipher_path[index]).is_ok();
    }
    if (is_root_copied && root_text.size() != 0) {
        // The configured path is operator-supplied, so it crosses the
        // trust boundary here and is declared external until the store's
        // own sanitizer promotes it.
        cipher_ = ::fixy::some(::fixy::mint_box<Cipher>(
            startup.cap().alloc, Cipher::open(startup, startup.cap().alloc,
                                              ::fixy::mint_tagged<::fixy::tags::source::External>(root_text.view()))));
    }

    bg_.set_region_ready_callback(this, [](void* self, ::foundation::effects::Bg const& bg,
                                           BackgroundThread::PublishStage stage,
                                           BackgroundThread::PublishedRegion const& published) noexcept {
        static_cast<Vigil*>(self)->on_region_ready(bg, stage, published);
    });

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
            startup.cap().alloc, ::crucible::warden::mint_deadline_watchdog(startup, &*senses, cfg_.watchdog_policy)));
        senses_ = ::fixy::some(std::move(senses));
    }

    bg_.start(&*ring_, &*meta_log_, cfg_.rank, cfg_.world_size, cfg_.device_capability);
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
    bool restored_one = false;
    ::fixy::WeakRef<RegionNode> restored{};
    bg_.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        restored_one = tx_log_.rollback(stage);
        if (!restored_one) return;
        // The job runs between two publications, so each publication after
        // it reads the mark, and none gives the foreground a region cut
        // before the rollback.
        bg_.request_mark(ops_before);
        const ::fixy::WeakRef<Transaction> active = tx_log_.active(stage);
        if (active.has_value()) {
            restored = ::fixy::WeakRef<RegionNode>::from_raw(active->region.value());
            // The foreground gets the region outside the publication slot,
            // so the store keeps it until the foreground writes its holders
            // again.
            bg_.keep_region(stage, ::fixy::WeakRef<const RegionNode>::from_raw(active->region.value()));
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
        register_externals_from_region_(fg, region);
        region_cache_.insert(region);
    }
    publish_held_regions_();
}

void Vigil::on_region_ready(::foundation::effects::Bg const& bg, BackgroundThread::PublishStage const& stage,
                            BackgroundThread::PublishedRegion const& published) {
    RegionNode& region = published.region.get();
    // bump returns the previous value, which is the index this call
    // reserved.  The background thread is the sole writer.
    const uint64_t step = step_.bump();

    Transaction& tx = tx_log_.begin_tx(stage, step);
    // The result is discarded because a state-machine logic error here
    // is not something the background thread can recover from.
    //
    // The merkle root goes through the checked accessor so the non-zero
    // invariant is witnessed at this call site: its precondition fires
    // here if the hash was never recomputed.
    (void)tx_log_.commit(stage, tx, ::fixy::mint_tagged<::fixy::tags::source::Arena>(&region), region.content_hash,
                         ::crucible::make_merkle_root(region.computed_merkle_hash()));
    (void)tx_log_.activate(stage, tx);

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

    // The observation is presented with a background-drain context
    // because this runs on the region-publishing thread, not the
    // foreground.
    for (::fixy::Box<::crucible::warden::DeadlineWatchdog>& watchdog : wd_) {
        const auto v = watchdog->observe(::fixy::BgDrainCtx{bg});
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
    }
}

}  // namespace crucible
