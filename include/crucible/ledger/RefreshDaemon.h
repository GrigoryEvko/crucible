#pragma once

// #67 — the thread that keeps the ledger current, and the policy of when
// to ask.
//
// Ledger.h already had everything a refresh needs: refresh_plan says what
// is missing or stale, run_refresh measures it through a probe table and
// folds the results in, commit_ledger writes it down. What it did not have
// was a schedule. The one-shot tool supplies that by being run at deploy.
// A long-lived process needs something that decides for itself, and the
// brief for it is one sentence: measured once, or every hour if unsure,
// and served from cache without remeasurements in between.
//
// Three rules follow from that sentence and they are the whole design.
//
//   1. A foreground reader never waits for a measurement. It takes the
//      published view, which is whatever the last successful refresh left
//      behind, and asks it questions. A refresh in flight is invisible to
//      it — the working ledger is the daemon's own, and it becomes visible
//      only when it is finished and committed.
//
//   2. A probe never runs on a host that is unfit to measure. Competence.h
//      already decides fitness; what this adds is that an unfit host is a
//      reason to SKIP rather than a reason to measure and grade low. The
//      distinction matters because measuring is not free: a probe under
//      load makes the load worse, and the result of that measurement is
//      refused anyway. Running it would be paying for a number the reader
//      is not allowed to use.
//
//   3. A refresh that produces nothing backs off. Nothing is the common
//      case on an unfit host and on a host whose probes keep missing the
//      variance bar, and retrying every hour forever would be a permanent
//      background load in exchange for a permanent stream of refusals. The
//      backoff doubles to a ceiling and resets the moment anything is
//      admitted.
//
// What this deliberately does NOT do is expire the served view when it
// goes stale. A stale entry stops being served by LedgerView::lookup on
// its own, because is_servable_at checks the TTL against the clock the
// view was minted with — and the daemon re-mints the view on every
// successful refresh. Adding a second expiry here would be a second
// opinion about the same question.
//
// Release behaviour of the checks in this file. The competence gate is a
// runtime branch and not an assertion, because it is a policy decision
// that has to hold in production — the build where NDEBUG strips
// CRUCIBLE_INVARIANT is exactly the build where an unfit host must not be
// probed. The contract on the mint's configuration fires in Release too:
// contracts build at `observe` there and the violation handler does not
// return, so a nonsensical schedule stops the process at construction
// rather than producing a thread that never sleeps.

#include <crucible/ledger/Ledger.h>
#include <crucible/ledger/ProbeSupport.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <semaphore>
#include <span>
#include <thread>
#include <utility>

namespace crucible::ledger {

// ── The context the daemon needs ──────────────────────────────────────
//
// Everything the store needs, plus the background effect. The extra
// conjunct is not decoration: the daemon spawns a thread and runs
// measurements on it, and a context that has not claimed Effect::Bg is
// one that promised not to. TestRunnerCtx is the case that makes this
// visible — it claims IO and Block and so satisfies CtxFitsLedgerStore,
// and it deliberately claims neither Bg nor Init, so it can read and
// write a ledger and cannot start the thread that refreshes one.

template <class Ctx>
concept CtxFitsRefreshDaemon =
    effects::CtxOwnsAllOf<Ctx, effects::Effect::IO, effects::Effect::Block, effects::Effect::Bg>;

static_assert(CtxFitsRefreshDaemon<LedgerIoCtx>);
static_assert(!CtxFitsRefreshDaemon<effects::HotFgCtx>, "a foreground context claims nothing and cannot refresh");
static_assert(!CtxFitsRefreshDaemon<effects::ColdInitCtx>, "initialization claims IO but not Block");
static_assert(!CtxFitsRefreshDaemon<effects::TestRunnerCtx>,
              "a fixture context claims IO and Block but not Bg, so it cannot stand in for a background thread");
static_assert(!CtxFitsRefreshDaemon<effects::BgCompileCtx>, "the compile context claims Bg and IO but not Block");

// ── The schedule ──────────────────────────────────────────────────────

struct RefreshSchedule {
    // The gap between one successful refresh and the next look. An hour,
    // which is also the default TTL, so a verdict that expires is looked
    // at about when it expires rather than an arbitrary time later.
    std::uint32_t idle_period_seconds = kDefaultTtlSeconds;

    // The first wait after a cycle that admitted nothing.
    std::uint32_t initial_backoff_seconds = 60;

    // The ceiling on that wait. Six hours: long enough that a host which
    // is permanently unfit costs almost nothing, short enough that one
    // which becomes fit overnight is noticed by morning.
    std::uint32_t max_backoff_seconds = 6u * 3600u;

    // Doubling. Stated as a field so a deployment can flatten it without
    // editing this header, and checked at the mint so it cannot be set to
    // one, which would make the backoff not back off.
    std::uint32_t backoff_multiplier = 2;

    [[nodiscard]] constexpr bool is_well_formed() const noexcept {
        return idle_period_seconds > 0u && initial_backoff_seconds > 0u
            && max_backoff_seconds >= initial_backoff_seconds && backoff_multiplier >= 2u;
    }

    // The next wait after `previous`, saturating at the ceiling.
    [[nodiscard]] constexpr std::uint32_t next_backoff(std::uint32_t previous) const noexcept {
        if (previous == 0u) {
            return initial_backoff_seconds;
        }
        const std::uint64_t doubled =
            static_cast<std::uint64_t>(previous) * static_cast<std::uint64_t>(backoff_multiplier);
        if (doubled >= static_cast<std::uint64_t>(max_backoff_seconds)) {
            return max_backoff_seconds;
        }
        return static_cast<std::uint32_t>(doubled);
    }
};

// ── One cycle's outcome ───────────────────────────────────────────────

enum class CycleResult : std::uint8_t {
    // Every wanted verdict was present, fresh and trusted. Nothing ran.
    NothingToDo = 0,
    // At least one verdict was measured and admitted, and the ledger was
    // committed.
    Admitted = 1,
    // Probes ran and nothing cleared the bar. The ledger is unchanged.
    AllRefused = 2,
    // The host was not fit to measure, so no probe was run at all.
    SkippedUnfitHost = 3,
    // The refresh admitted something and the commit failed. The in-memory
    // ledger is ahead of the file, which the next cycle will try again.
    CommitFailed = 4,
};

[[nodiscard]] constexpr std::string_view cycle_result_name(CycleResult result) noexcept {
    switch (result) {
        case CycleResult::NothingToDo:
            return "nothing-to-do";
        case CycleResult::Admitted:
            return "admitted";
        case CycleResult::AllRefused:
            return "all-refused";
        case CycleResult::SkippedUnfitHost:
            return "skipped-unfit-host";
        case CycleResult::CommitFailed:
            return "commit-failed";
        default:
            return "<unknown CycleResult>";
    }
}

// Whether a cycle's outcome means the next one should wait the idle
// period or back off. Stated as a function so the daemon's loop and its
// test agree by construction rather than by both getting it right.
[[nodiscard]] constexpr bool outcome_resets_backoff(CycleResult result) noexcept {
    return result == CycleResult::Admitted || result == CycleResult::NothingToDo;
}

struct CycleReport {
    CycleResult result = CycleResult::NothingToDo;
    std::uint32_t queued_count = 0;
    RefreshOutcome outcome{};
    std::uint16_t competence_defects = 0;
};

// Rule 2, in one place. A probe runs only on a fit host, and an unfit one
// is a skip rather than a low-confidence measurement.
//
// Split out from the daemon so a test can drive it with a synthetic
// competence report and no thread, and so the rule has a name a reader can
// grep for rather than being a condition inside a loop.
[[nodiscard]] inline CycleReport refresh_when_fit(Ledger& ledger, std::span<const VerdictId> wanted,
                                                  std::span<const ProbeRegistration> registry,
                                                  CompetenceReport const& competence,
                                                  std::uint64_t now_unix_seconds) noexcept {
    CycleReport report{};
    report.competence_defects = competence.defect_word();

    const RefreshQueue queue = refresh_plan(ledger, wanted, now_unix_seconds);
    report.queued_count = static_cast<std::uint32_t>(queue.size());
    if (queue.empty()) {
        report.result = CycleResult::NothingToDo;
        return report;
    }

    if (!competence.is_competent()) {
        // The measurement would be refused at admission anyway, and
        // running it would add load to a host that is already too loaded
        // to measure on. Skipping is strictly better than both.
        report.result = CycleResult::SkippedUnfitHost;
        return report;
    }

    report.outcome = run_refresh(ledger, queue, registry, competence, now_unix_seconds);
    report.result = (report.outcome.admitted_count > 0u) ? CycleResult::Admitted : CycleResult::AllRefused;
    return report;
}

// ── The daemon ────────────────────────────────────────────────────────

struct RefreshDaemonConfig {
    RefreshSchedule schedule{};

    // What to keep current. An empty span means every verdict this build
    // knows about has a probe and should be kept — spelled by the caller
    // rather than defaulted here, because a deployment that only cares
    // about two verdicts should not pay for eight.
    std::span<const VerdictId> wanted{};
    std::span<const ProbeRegistration> registry{};

    // Measurement parameters for the probes, applied once at start. The
    // daemon writes them because the probes read them from one place and
    // nothing else in a long-lived process sets them.
    ProbeSettings probe_settings{};
};

class RefreshDaemon {
public:
    RefreshDaemon(RefreshDaemonConfig config, HostFacts facts, HostFingerprint fingerprint, Ledger seed) noexcept
        : config_{config},
          facts_{facts},
          fingerprint_{fingerprint},
          working_{std::move(seed)} {
        publish_(working_);
    }

    RefreshDaemon(RefreshDaemon const&) = delete("the refresh thread captures `this`; a copy would share it");
    RefreshDaemon& operator=(RefreshDaemon const&) = delete("the refresh thread captures `this`; a copy would share it");
    RefreshDaemon(RefreshDaemon&&) = delete("the refresh thread captures `this`, so the address must not move");
    RefreshDaemon& operator=(RefreshDaemon&&) = delete("the refresh thread captures `this`, so the address must not move");

    ~RefreshDaemon() noexcept { stop(); }

    void start() noexcept {
        if (thread_.joinable()) {
            return;
        }
        set_probe_settings(config_.probe_settings);
        // Drain a wake token left behind by a stop() whose loop had
        // already exited. Without this, the first cycle after a restart
        // consumes the stale token and skips its wait entirely, so a
        // daemon that was stopped and started again measures twice in a
        // row on a host it should have left alone.
        (void)wakeup_.try_acquire();
        thread_ = std::jthread{[this](std::stop_token token) { loop_(token); }};
    }

    // Waits for a cycle already in flight. A measurement cannot be
    // abandoned halfway — the probes hold mapped regions and helper
    // threads — so a stop during a refresh takes as long as the refresh
    // has left, which for the cache-tier sweep is tens of seconds. The
    // destructor calls this, so the same wait applies there.
    void stop() noexcept {
        if (!thread_.joinable()) {
            return;
        }
        thread_.request_stop();
        // Wakes the sleeper immediately rather than leaving the caller to
        // wait out whatever remains of an idle period that can be six
        // hours long.
        wakeup_.release();
        thread_.join();
    }

    // The foreground read. Never waits for a measurement: it takes the
    // most recently published view, which the daemon replaces whole after
    // a successful commit and never edits in place.
    //
    // "Never blocks" means precisely this: no path from here reaches the
    // daemon's mutex-free working state, its probes, or the disk. The
    // shared-pointer load can contend with another load or with the
    // daemon's store for as long as one atomic operation takes, which is
    // bounded and is not waiting on work.
    [[nodiscard]] std::shared_ptr<const LedgerView> current_view() const noexcept {
        return published_.load(std::memory_order_acquire);
    }

    // How many cycles have finished, of any outcome. Lets a test wait for
    // progress without sleeping for a schedule's worth of wall time.
    [[nodiscard]] std::uint64_t completed_cycle_count() const noexcept {
        return cycle_count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] CycleResult last_result() const noexcept { return last_result_.load(std::memory_order_acquire); }

    [[nodiscard]] std::uint32_t current_backoff_seconds() const noexcept {
        return backoff_seconds_.load(std::memory_order_acquire);
    }

    // Runs exactly one cycle on the CALLING thread. The daemon's loop is
    // this function plus a sleep, so a test exercises the real thing
    // rather than a copy of it.
    CycleReport run_one_cycle() noexcept {
        // Mutates the working ledger, so two at once would race on it.
        // The loop calls this and a caller may call it directly, and
        // nothing stops someone doing both. The exchange is the check:
        // it fires in Release, where NDEBUG has already stripped every
        // debug-only assertion and this is the build a race would
        // actually happen in.
        const bool was_already_running = cycle_in_flight_.exchange(true, std::memory_order_acq_rel);
        contract_assert(!was_already_running);
        const CycleGuard guard{cycle_in_flight_};

        const std::uint64_t now = wall_clock_unix_seconds();
        if (now == 0u) {
            CycleReport report{};
            report.result = CycleResult::AllRefused;
            return report;
        }

        const CompetenceReport competence = probe_competence();
        // The seed came either from seed_ledger, which stamps this
        // fingerprint, or from load_ledger, which refuses a file whose
        // record disagrees with the name it was found under. A mismatch
        // here means the working ledger has been overwritten with another
        // host's, and committing it would publish that host's verdicts
        // under this one's name.
        contract_assert(working_.fingerprint == fingerprint_);
        working_.competence = competence;
        working_.cpu_vendor = facts_.cpu_vendor;
        working_.cpu_model = facts_.cpu_model;

        CycleReport report = refresh_when_fit(working_, config_.wanted, config_.registry, competence, now);

        if (report.result == CycleResult::Admitted) {
            constexpr LedgerIoCtx io_ctx{};
            if (!commit_ledger(io_ctx, working_).has_value()) {
                report.result = CycleResult::CommitFailed;
            }
            publish_(working_);
        }

        cycle_count_.fetch_add(1, std::memory_order_release);
        last_result_.store(report.result, std::memory_order_release);
        return report;
    }

private:
    // Clears the in-flight flag however run_one_cycle leaves, including
    // the early return on a dead clock.
    struct CycleGuard {
        std::atomic<bool>& flag;
        ~CycleGuard() noexcept { flag.store(false, std::memory_order_release); }
    };

    void publish_(Ledger const& ledger) noexcept {
        const std::uint64_t now = wall_clock_unix_seconds();
        auto view = std::make_shared<const LedgerView>(ledger, now);
        published_.store(std::move(view), std::memory_order_release);
    }

    void loop_(std::stop_token token) noexcept {
        while (!token.stop_requested()) {
            const CycleReport report = run_one_cycle();

            std::uint32_t wait_seconds = config_.schedule.idle_period_seconds;
            if (outcome_resets_backoff(report.result)) {
                backoff_seconds_.store(0, std::memory_order_release);
            } else {
                const std::uint32_t next =
                    config_.schedule.next_backoff(backoff_seconds_.load(std::memory_order_acquire));
                backoff_seconds_.store(next, std::memory_order_release);
                wait_seconds = next;
            }

            // try_acquire_for and not sleep_for: stop() releases the
            // semaphore, so a shutdown is noticed at once instead of after
            // however much of a six-hour backoff is left.
            (void)wakeup_.try_acquire_for(std::chrono::seconds{wait_seconds});
        }
    }

    RefreshDaemonConfig config_{};
    HostFacts facts_{};
    HostFingerprint fingerprint_{};

    // The daemon's own copy. A foreground reader never sees it; it sees
    // whatever publish_ last handed over.
    Ledger working_{};

    std::atomic<std::shared_ptr<const LedgerView>> published_{};
    std::atomic<std::uint64_t> cycle_count_{0};
    std::atomic<CycleResult> last_result_{CycleResult::NothingToDo};
    std::atomic<std::uint32_t> backoff_seconds_{0};
    std::atomic<bool> cycle_in_flight_{false};

    std::binary_semaphore wakeup_{0};
    std::jthread thread_{};
};

// ── The mint ──────────────────────────────────────────────────────────
//
// §XXI ctx-bound mint. The context is the first parameter, the
// requires-clause is one named concept, and what comes back is a concrete
// type whose contents are trusted from then on.
//
// It returns a unique_ptr rather than a value because the refresh thread
// captures `this`, so the daemon cannot be moved after construction and a
// by-value return would have to move it. §XXI allows an allocating mint to
// drop constexpr, and the marker on the signature line is how the scanner
// is told so.
template <effects::IsExecCtx Ctx>
    requires CtxFitsRefreshDaemon<Ctx>
[[nodiscard]] inline std::unique_ptr<RefreshDaemon> mint_refresh_daemon(  // MINT-PATTERN-OK: allocating
    Ctx const& ctx, RefreshDaemonConfig config) noexcept
    // Fires in Release. A schedule whose backoff does not back off, or
    // whose idle period is zero, produces a thread that measures without
    // pause on a host that cannot measure, and there is no safe way to
    // continue from it.
    pre(config.schedule.is_well_formed())
    pre(!config.registry.empty())
{
    const HostFacts facts = probe_host_facts();
    const HostFingerprint fingerprint = fold_fingerprint(facts);
    const CompetenceReport competence = probe_competence();

    // Start from what is on disk for this fingerprint, or from a clean
    // ledger carrying this host's identity. A fingerprint change is never
    // a merge: the old file keeps its own name and this one starts empty.
    Ledger seed = seed_ledger(facts, fingerprint, competence);
    if (fingerprint.is_complete()) {
        auto loaded = load_ledger(ctx, fingerprint);
        if (loaded.has_value()) {
            seed = std::move(*loaded);
        }
    }
    return std::make_unique<RefreshDaemon>(config, facts, fingerprint, std::move(seed));
}

namespace refresh_daemon_detail::self_test {

// The backoff doubles and then stops doubling.
inline constexpr RefreshSchedule s_schedule{
    .idle_period_seconds = 3600,
    .initial_backoff_seconds = 60,
    .max_backoff_seconds = 480,
    .backoff_multiplier = 2,
};
static_assert(s_schedule.is_well_formed());
static_assert(s_schedule.next_backoff(0) == 60, "the first backoff is the initial one, not twice it");
static_assert(s_schedule.next_backoff(60) == 120);
static_assert(s_schedule.next_backoff(120) == 240);
static_assert(s_schedule.next_backoff(240) == 480);
static_assert(s_schedule.next_backoff(480) == 480, "the ceiling holds");
// A previous wait already past the ceiling stays at the ceiling rather
// than overflowing back down.
static_assert(s_schedule.next_backoff(0xFFFFFFFFu) == 480);

// A multiplier of one would not back off, so it is not well formed and
// the mint's precondition refuses it.
static_assert(!RefreshSchedule{.backoff_multiplier = 1}.is_well_formed());
static_assert(!RefreshSchedule{.idle_period_seconds = 0}.is_well_formed());
static_assert(!RefreshSchedule{.initial_backoff_seconds = 0}.is_well_formed());
static_assert(!RefreshSchedule{.initial_backoff_seconds = 600, .max_backoff_seconds = 60}.is_well_formed(),
              "a ceiling below the floor is not a range");
static_assert(RefreshSchedule{}.is_well_formed(), "the default schedule has to be usable as written");

// Only a cycle that got somewhere resets the backoff. A skipped cycle on
// an unfit host must back off, or an unfit host is probed every idle
// period forever.
static_assert(outcome_resets_backoff(CycleResult::Admitted));
static_assert(outcome_resets_backoff(CycleResult::NothingToDo));
static_assert(!outcome_resets_backoff(CycleResult::AllRefused));
static_assert(!outcome_resets_backoff(CycleResult::SkippedUnfitHost));
static_assert(!outcome_resets_backoff(CycleResult::CommitFailed));

static_assert(!std::is_copy_constructible_v<RefreshDaemon>);
static_assert(!std::is_move_constructible_v<RefreshDaemon>);

}  // namespace refresh_daemon_detail::self_test

}  // namespace crucible::ledger
