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

#include <filesystem>
#include <memory>
#include <utility>

namespace crucible {

Vigil::Vigil(Config cfg, ::fixy::InitLoadCtx const& startup) : cfg_(std::move(cfg)), tx_log_{startup} {
    ring_ = std::make_unique<TraceRing>();
    ring_->reset();

    meta_log_ = std::make_unique<MetaLog>();
    meta_log_->reset();

    if (!cfg_.cipher_path.empty()) {
        // The configured path is operator-supplied, so it crosses the
        // trust boundary here and is declared external until the store's
        // own sanitizer promotes it.
        cipher_ = std::make_unique<Cipher>(Cipher::open(
            startup, ::fixy::mint_tagged<::fixy::tags::source::External>(std::filesystem::path{cfg_.cipher_path})));
    }

    bg_.set_region_ready_callback(
        this, [](void* self, ::foundation::effects::Bg const& bg, BackgroundThread::PublishStage stage,
                 RegionNode* region) noexcept { static_cast<Vigil*>(self)->on_region_ready(bg, stage, region); });

    // The watchdog is constructed before the background thread starts,
    // so on_region_ready can observe on the very first region transition
    // without a null check.  Attach failure is not an error here: every
    // observation then returns InsufficientData.
    if (cfg_.enable_deadline_watchdog) {
        senses_ = std::make_unique<::crucible::perf::Senses>(
            ::crucible::perf::Senses::load_subset(startup, ::crucible::perf::SensesMask{.sched_switch = true}));
        wd_ = std::make_unique<::crucible::warden::DeadlineWatchdog>(
            ::crucible::warden::mint_deadline_watchdog(startup, senses_.get(), cfg_.watchdog_policy));
    }

    bg_.start(ring_.get(), meta_log_.get(), cfg_.rank, cfg_.world_size, cfg_.device_capability);
}

Vigil::~Vigil() = default;

ContentHash Vigil::head_hash() const noexcept { return cipher_ ? cipher_->head() : ContentHash{}; }

bool Vigil::rollback() {
    bool restored_one = false;
    RegionNode* restored = nullptr;
    bg_.run_on_publish_stage([&](BackgroundThread::PublishStage const& stage) noexcept {
        restored_one = tx_log_.rollback(stage);
        if (!restored_one) return;
        if (const Transaction* tx = tx_log_.active(stage)) restored = tx->region.value();
    });
    if (!restored_one) return false;
    const VigilFgCtx fg = assert_producer_thread_();
    // The deactivation abandons the replayed region the way a divergence
    // does, so the mode takes the same transition.  A restored region
    // with a memory plan takes it back to COMPILED below.
    if (ctx_.is_compiled()) ctx_.deactivate();
    mode_.publish_recording_after_divergence();
    if (restored != nullptr) {
        bg_.active_region.store(restored, std::memory_order_release);
        if (ctx_.activate(restored)) {
            register_externals_from_region_(fg, restored);
            mode_.publish_compiled();
        }
    }
    return true;
}

void Vigil::on_region_ready(::foundation::effects::Bg const& bg, BackgroundThread::PublishStage const& stage,
                            RegionNode* region) {
    // bump returns the previous value, which is the index this call
    // reserved.  The background thread is the sole writer.
    const uint64_t step = step_.bump();

    auto* tx = tx_log_.begin_tx(stage, step);
    // The result is discarded because a state-machine logic error here
    // is not something the background thread can recover from.
    //
    // The merkle root goes through the checked accessor so the non-zero
    // invariant is witnessed at this call site: its precondition fires
    // here if the hash was never recomputed.
    (void)tx_log_.commit(stage, tx, ::fixy::mint_tagged<::fixy::tags::source::Arena>(region), region->content_hash,
                         ::crucible::make_merkle_root(region->computed_merkle_hash()));
    (void)tx_log_.activate(stage, tx);

    // Publishing the region signals the foreground, which picks it up
    // on its next dispatch.  The mode stays RECORDING here.  The
    // foreground still records until it aligns to this region, and it
    // publishes COMPILED when it activates the context, so is_compiled
    // never reports a replay that has not started.  An observer that
    // waits for this publication reads has_pending_region.
    pending_region_.publish(region);

    // The observation is presented with a background-drain context
    // because this runs on the region-publishing thread, not the
    // foreground.
    if (wd_) {
        const auto v = wd_->observe(::fixy::BgDrainCtx{bg});
        wd_last_verdict_.store(v, std::memory_order_release);
        switch (v) {
            case ::crucible::warden::WatchdogVerdict::Healthy:
                wd_healthy_count_.fetch_add(1, std::memory_order_release);
                break;
            case ::crucible::warden::WatchdogVerdict::Downgrade:
                wd_downgrade_count_.fetch_add(1, std::memory_order_release);
                break;
            case ::crucible::warden::WatchdogVerdict::InsufficientData:
                wd_insufficient_count_.fetch_add(1, std::memory_order_release);
                break;
            default:
                // The verdict enum has exactly the three values handled
                // above, so this arm is deleted in release.
                std::unreachable();
        }
    }
}

}  // namespace crucible
