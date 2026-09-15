#pragma once

// The hardware-capability ledger, as the rest of Crucible sees it.
//
// Two audiences, two shapes.
//
//   The runtime reads. It opens the ledger once, asks questions, and gets
//   back either a measured answer it can trust or the conservative answer it
//   named at the call site. It never measures and never writes.
//
//   A writer refreshes. It asks refresh_plan what is missing or stale, runs
//   the probes for those questions, and commits. crucible-hwprobe is one
//   such writer and runs once at deploy. The in-process refresh thread is
//   another and is #67; it is not built here, and everything it needs is.
//
// The seam between them is refresh_plan plus the ProbeRegistration table.
// #67 does not need a new entry point: it calls the same planner the tool
// calls, supplies the same kind of probe table, and commits through the same
// store. What #67 adds is a thread, a schedule and a backoff — none of which
// belong to the mechanism.
//
// Probes themselves are #68-#70. This header defines the signature they
// implement and nothing else; there is exactly one probe in the tree today
// and it lives in the tool, because its only job is to prove the loop
// closes.
//
// DetSafe (axiom 8). A verdict is allowed to change how fast the runtime
// goes. It is never allowed to change what the runtime computes. Concretely:
// no ledger symbol may become reachable from content_hash, from merkle_hash,
// or from the memory plan, and a BITEXACT recipe pins its path regardless of
// any verdict this ledger could serve. CogMimic already excludes calibrated
// throughput from its cache key for the same reason, and that exclusion is
// preserved — the ledger folds the caps CLASS into its fingerprint, never
// the other way round. scripts/check-detsafe-ledger.sh walks the include
// closure of the hashing path and fails if a ledger header appears in it.

#include <crucible/ledger/Competence.h>
#include <crucible/ledger/HostFingerprint.h>
#include <crucible/ledger/LedgerStore.h>
#include <crucible/ledger/Verdict.h>

#include <cstdint>
#include <expected>
#include <inplace_vector>
#include <span>
#include <string_view>
#include <utility>

namespace crucible::ledger {

// ── The read side ─────────────────────────────────────────────────────

// A loaded ledger plus the clock to judge it against. Copyable and cheap to
// hold; the runtime opens one at startup and keeps it.
//
// There is no accessor that yields a bare value. Every path out goes
// through VerdictLookup, which makes the caller name its conservative
// answer. That is the whole fail-closed contract, and it is enforced by the
// absence of an alternative rather than by a rule someone has to remember.
class LedgerView {
public:
    constexpr LedgerView() noexcept = default;

    explicit LedgerView(Ledger ledger, std::uint64_t now_unix_seconds) noexcept
        : ledger_{std::move(ledger)}, now_unix_seconds_{now_unix_seconds}, is_loaded_{true} {}

    // An empty view. Every lookup misses, so every consumer takes its
    // conservative path. This is what a host with no ledger yet gets, and
    // it is a correct state rather than an error one.
    [[nodiscard]] static LedgerView empty(LedgerError why) noexcept {
        LedgerView view{};
        view.miss_reason_ = why;
        return view;
    }

    [[nodiscard]] bool is_loaded() const noexcept { return is_loaded_; }
    [[nodiscard]] HostFingerprint fingerprint() const noexcept { return ledger_.fingerprint; }
    [[nodiscard]] CompetenceReport const& competence() const noexcept { return ledger_.competence; }
    [[nodiscard]] std::size_t entry_count() const noexcept { return ledger_.entries.size(); }

    // The default question. Answers only from a high-confidence, unexpired
    // entry. A low-confidence entry is present in the file and is
    // deliberately not served here: the host that produced it was not fit to
    // measure, and serving it would be indistinguishable to the caller from
    // serving a good one.
    [[nodiscard]] VerdictLookup lookup(VerdictId id) const noexcept {
        if (!is_loaded_) {
            return VerdictLookup::unknown(miss_reason_);
        }
        const LedgerEntry* entry = ledger_.find(id);
        if (entry == nullptr) {
            return VerdictLookup::unknown(LedgerError::UnknownVerdictId);
        }
        if (!entry->is_servable_at(now_unix_seconds_)) {
            return VerdictLookup::unknown(LedgerError::ConfidenceBelowBar);
        }
        return VerdictLookup::known(*entry);
    }

    // The explicit escape hatch, for a caller that genuinely wants to see a
    // low-confidence number and has somewhere to put the caveat — a
    // diagnostic dump, an operator report, a heuristic that already treats
    // its input as a hint. The returned lookup carries the grade, so a
    // caller that forgets to branch on confidence() still cannot mistake it
    // for a trusted answer: the name of this function is at the call site.
    [[nodiscard]] VerdictLookup lookup_including_low(VerdictId id) const noexcept {
        if (!is_loaded_) {
            return VerdictLookup::unknown(miss_reason_);
        }
        const LedgerEntry* entry = ledger_.find(id);
        if (entry == nullptr) {
            return VerdictLookup::unknown(LedgerError::UnknownVerdictId);
        }
        if (entry->is_expired_at(now_unix_seconds_)) {
            return VerdictLookup::unknown(LedgerError::ConfidenceBelowBar);
        }
        return VerdictLookup::known(*entry);
    }

    [[nodiscard]] Ledger const& raw_ledger() const noexcept { return ledger_; }
    [[nodiscard]] std::uint64_t now_unix_seconds() const noexcept { return now_unix_seconds_; }

private:
    Ledger ledger_{};
    std::uint64_t now_unix_seconds_ = 0;
    bool is_loaded_ = false;
    LedgerError miss_reason_ = LedgerError::StoreReadFailed;
};

// The runtime's read handle, and the ledger's one §XXI mint.
//
// It is a mint rather than an `open_*` because it is the cross-tier
// authorization point: it takes an execution context, checks at the type
// level that the context may perform blocking I/O, and synthesizes a fresh
// LedgerView whose contents are trusted from then on. Every subsequent
// lookup on that view is concept-free and runs at full speed.
//
// load_ledger and commit_ledger are deliberately NOT mints. They act on
// paths and bytes and synthesize no authority of their own — the same
// reason fixy::fs::commit_atomic documents itself as not a mint.
//
// A missing or unreadable file is not an error the caller has to handle: it
// yields an empty view, every lookup misses, and every consumer takes its
// conservative path. That is the only behaviour under which a first boot, a
// corrupted cache and a fresh kernel upgrade all produce correct-but-slower
// rather than wrong.
//
// The mint is not constexpr: opening and reading a file invokes the
// kernel, and constexpr would lie about the cost. §XXI allows that for an
// allocating mint, and the marker on the signature line is how the
// scanner is told so.
template <effects::IsExecCtx Ctx>
    requires CtxFitsLedgerStore<Ctx>
[[nodiscard]] inline LedgerView mint_ledger_view(  // MINT-PATTERN-OK: allocating
    Ctx const& ctx, HostFingerprint fingerprint) noexcept {
    const std::uint64_t now = wall_clock_unix_seconds();
    if (now == 0u) {
        return LedgerView::empty(LedgerError::ClockUnavailable);
    }
    if (!fingerprint.is_complete()) {
        return LedgerView::empty(LedgerError::FingerprintMismatch);
    }
    auto loaded = load_ledger(ctx, fingerprint);
    if (!loaded.has_value()) {
        return LedgerView::empty(loaded.error());
    }
    return LedgerView{std::move(*loaded), now};
}

// The fingerprint-probing overload. Same carve-out: it reads sysfs and
// then a file.
template <effects::IsExecCtx Ctx>
    requires CtxFitsLedgerStore<Ctx>
[[nodiscard]] inline LedgerView mint_ledger_view(  // MINT-PATTERN-OK: allocating
    Ctx const& ctx) noexcept {
    return mint_ledger_view(ctx, probe_host_fingerprint());
}

// ── The write side, and the seam for #67 ──────────────────────────────

enum class RefreshReason : std::uint8_t {
    Absent = 0,  // never measured on this fingerprint
    Expired = 1,  // measured, but past its TTL
    LowConfidence = 2,  // measured on a host that was not fit at the time
};

[[nodiscard]] constexpr std::string_view refresh_reason_name(RefreshReason reason) noexcept {
    switch (reason) {
        case RefreshReason::Absent:
            return "absent";
        case RefreshReason::Expired:
            return "expired";
        case RefreshReason::LowConfidence:
            return "low-confidence";
        default:
            return "<unknown RefreshReason>";
    }
}

struct RefreshRequest {
    VerdictId id = VerdictId::TimerFloorNanos;
    RefreshReason reason = RefreshReason::Absent;
};

using RefreshQueue = std::inplace_vector<RefreshRequest, kMaxLedgerEntries>;

// What needs measuring, and why. This is the seam: #67's refresh thread
// calls this on a timer, gets back a queue, and decides how much of it to
// work through and how long to wait before asking again. The tool calls the
// same function and works through all of it at once. Neither owns the
// policy of when to ask — that is exactly what #67 adds.
//
// A low-confidence entry is queued for refresh even though it is present,
// because the reason it is low is usually transient: the host was loaded, or
// the operator had not pinned the clock yet. Asking again later is how it
// gets promoted.
[[nodiscard]] inline RefreshQueue refresh_plan(Ledger const& ledger, std::span<const VerdictId> wanted,
                                               std::uint64_t now_unix_seconds) noexcept {
    RefreshQueue queue{};
    for (const VerdictId id : wanted) {
        if (queue.size() >= queue.capacity()) {
            break;
        }
        const LedgerEntry* entry = ledger.find(id);
        if (entry == nullptr) {
            queue.push_back(RefreshRequest{.id = id, .reason = RefreshReason::Absent});
            continue;
        }
        if (entry->is_expired_at(now_unix_seconds)) {
            queue.push_back(RefreshRequest{.id = id, .reason = RefreshReason::Expired});
            continue;
        }
        if (entry->confidence != Confidence::High) {
            queue.push_back(RefreshRequest{.id = id, .reason = RefreshReason::LowConfidence});
        }
    }
    return queue;
}

// Everything a probe produces. The probe does not decide whether its own
// number is trustworthy — admit_entry does, against the competence report,
// and a probe that could grade itself could grade itself generously.
struct VerdictMeasurement {
    VerdictValue value{};
    VerdictEvidence evidence{};
};

// The signature #68-#70 implement. A free function pointer rather than a
// type-erased callable: the table is static, the call is once per refresh,
// and a plain pointer keeps the registration a compile-time constant that a
// reader can follow to the definition.
using ProbeFunction = std::expected<VerdictMeasurement, LedgerError> (*)(CompetenceReport const&) noexcept;

struct ProbeRegistration {
    VerdictId id = VerdictId::TimerFloorNanos;
    ProbeFunction run = nullptr;
};

[[nodiscard]] inline ProbeFunction find_probe(std::span<const ProbeRegistration> registry, VerdictId id) noexcept {
    for (ProbeRegistration const& registration : registry) {
        if (registration.id == id && registration.run != nullptr) {
            return registration.run;
        }
    }
    return nullptr;
}

// What happened to one queued verdict. A refusal carries the bar it missed
// and the evidence that missed it, because a count of refusals with no
// reason is the failure this ledger exists to prevent.
struct RefreshRecord {
    VerdictId id = VerdictId::TimerFloorNanos;
    bool was_admitted = false;
    bool had_probe = false;
    EvidenceFault fault = EvidenceFault::None;
    LedgerError error = LedgerError::None;
    VerdictEvidence evidence{};
};

using RefreshLog = std::inplace_vector<RefreshRecord, kMaxLedgerEntries>;

struct RefreshOutcome {
    std::uint32_t measured_count = 0;
    std::uint32_t admitted_count = 0;
    std::uint32_t refused_count = 0;  // measured, but the evidence did not hold up
    std::uint32_t no_probe_count = 0;  // queued, but nothing registered to answer it
    RefreshLog log{};
};

// Runs the queued probes and folds the results into `ledger`. Does not
// commit; the caller decides whether a run that admitted nothing is worth a
// write. Shared verbatim between the tool and #67 so the two cannot drift
// into disagreeing about what a refresh does.
[[nodiscard]] inline RefreshOutcome run_refresh(Ledger& ledger, RefreshQueue const& queue,
                                                std::span<const ProbeRegistration> registry,
                                                CompetenceReport const& competence,
                                                std::uint64_t now_unix_seconds) noexcept {
    RefreshOutcome outcome{};
    for (RefreshRequest const& request : queue) {
        RefreshRecord record{.id = request.id};

        const ProbeFunction probe = find_probe(registry, request.id);
        if (probe == nullptr) {
            ++outcome.no_probe_count;
            record.error = LedgerError::UnknownVerdictId;
            if (outcome.log.size() < outcome.log.capacity()) {
                outcome.log.push_back(record);
            }
            continue;
        }
        record.had_probe = true;

        auto measured = probe(competence);
        ++outcome.measured_count;
        if (!measured.has_value()) {
            ++outcome.refused_count;
            record.error = measured.error();
            if (outcome.log.size() < outcome.log.capacity()) {
                outcome.log.push_back(record);
            }
            continue;
        }
        record.evidence = measured->evidence;
        record.fault = audit_evidence(measured->evidence);

        // The judging happens here and only here. A probe cannot grade its
        // own output, and a number that does not clear the bar never
        // becomes an entry.
        auto entry = admit_entry(request.id, measured->value, measured->evidence, competence, now_unix_seconds);
        if (!entry.has_value()) {
            ++outcome.refused_count;
            record.error = entry.error();
        } else if (auto placed = ledger.upsert(*entry); !placed.has_value()) {
            ++outcome.refused_count;
            record.error = placed.error();
        } else {
            ++outcome.admitted_count;
            record.was_admitted = true;
        }
        if (outcome.log.size() < outcome.log.capacity()) {
            outcome.log.push_back(record);
        }
    }
    return outcome;
}

// Builds the ledger a writer should start from: the entries already on disk
// for this fingerprint, or an empty one carrying the current host's
// identity. A fingerprint change is therefore not a merge — the old file
// stays where it is and a new one starts clean, which is the only correct
// answer when the thing measured has changed.
[[nodiscard]] inline Ledger seed_ledger(HostFacts const& facts, HostFingerprint fingerprint,
                                        CompetenceReport const& competence) noexcept {
    Ledger ledger{};
    ledger.fingerprint = fingerprint;
    ledger.competence = competence;
    ledger.cpu_vendor = facts.cpu_vendor;
    ledger.cpu_model = facts.cpu_model;
    return ledger;
}

namespace ledger_detail::self_test {

// An empty view answers every question with the caller's conservative value
// and never with a zero it invented.
inline const VerdictLookup s_from_empty =
    LedgerView::empty(LedgerError::StoreReadFailed).lookup(VerdictId::TimerFloorNanos);

static_assert(std::is_same_v<decltype(LedgerView{}.lookup(VerdictId::TimerFloorNanos)), VerdictLookup>,
              "the only way out of a LedgerView is a VerdictLookup");

// VerdictLookup must not grow a bare accessor. If one is ever added, the
// fail-closed contract is gone: a caller could read a value without naming
// a fallback, and a miss would hand back a default-constructed zero that
// looks exactly like a measured zero.
template <class T>
concept HasBareValueAccessor = requires(T const& lookup) { lookup.value(); };
static_assert(!HasBareValueAccessor<VerdictLookup>,
              "VerdictLookup must never expose value(); use value_or_conservative()");

template <class T>
concept HasDereference = requires(T const& lookup) { *lookup; };
static_assert(!HasDereference<VerdictLookup>, "VerdictLookup must never expose operator*");

}  // namespace ledger_detail::self_test

}  // namespace crucible::ledger
