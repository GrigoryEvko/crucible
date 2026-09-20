#pragma once

// Where the ledger lives, and how it gets there intact.
//
// One file per fingerprint under the XDG cache directory. Both halves of the
// fingerprint are in the filename, so a governor flip lands in a new file
// instead of overwriting a good one, and flipping the governor back finds
// the old entries still there.
//
// Writes are tmp-then-fsync-then-rename. A reader therefore sees either the
// whole previous ledger or the whole new one, never a half-written line.
//
// The descriptor work goes through crucible::safety (handles/FileHandle.h),
// whose syscall sites are already covered by the capability allowlist.
// fixy::fs offers a higher-level version of the same dance and would have
// been the tidier dependency, but it is under active rewrite and currently
// does not compile; a cache mechanism should not be hostage to another
// subsystem's edit cycle. The one syscall this header issues directly is
// fsync, which has no typed wrapper in the tree; it carries an inline
// capability marker rather than a shared-allowlist entry, which is the
// sanctioned form for a site inside a Ctx-gated boundary.
//
// Not Cipher. Cipher is 1,263 lines of event-sourced, tiered, federated
// state whose store path has no production caller, and the brief for this
// mechanism says lightweight. The ledger is a cache: losing it costs one
// remeasure. If durability or an audit trail is ever wanted — who measured
// this, on which build, and what did the previous value say — Cipher is the
// migration target and the entry format below is already a flat record set
// that maps onto a Cipher event log without reshaping.
//
// The format is text. At tens of entries read once per process start there
// is nothing to win from a packed layout, and there is plenty to lose: an
// operator debugging a verdict that looks wrong can cat the file, grep it,
// and paste it into a bug report, and there is no struct padding or
// endianness to get wrong between a writer and a reader built differently.
//
// DetSafe (axiom 8): a store path, a TTL and a confidence grade can change
// how fast the runtime goes and must never change what it computes.
// scripts/check-detsafe-ledger.sh asserts no ledger symbol is reachable
// from content_hash, merkle_hash or the memory plan.

#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/handles/FileHandle.h>
#include <crucible/ledger/Competence.h>
#include <crucible/ledger/HostFingerprint.h>
#include <crucible/ledger/Verdict.h>
#include <crucible/safety/_Path.h>
#include <crucible/safety/sanitize/PathTraversal.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <inplace_vector>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace crucible::ledger {

// ── The execution context the store needs ─────────────────────────────
//
// A disk read blocks, and cap_permitted_row<Init> deliberately does not
// admit Effect::Block: initialization is not allowed to park. So the store
// runs on a background capability even when the caller is a one-shot tool
// at process start. That is the type system being right rather than
// inconvenient — the read genuinely blocks, and pretending otherwise is how
// a startup path acquires a disk stall nobody budgeted for.

using LedgerIoCtx = effects::ExecCtx<
    effects::Bg, effects::ctx_numa::Spread, effects::ctx_alloc::Heap, effects::ctx_heat::Cold, effects::ctx_resid::DRAM,
    effects::Row<effects::Effect::Bg, effects::Effect::Alloc, effects::Effect::IO, effects::Effect::Block>>;

static_assert(sizeof(LedgerIoCtx) == 1, "an execution context must stay a tag");

// Every store entry point takes a context admitting both IO and Block,
// because every one of them opens a file and waits on a disk.
//
// Written through CtxOwnsAllOf rather than as a hand-rolled conjunction of
// row_contains_v: the named lift costs the same and makes the shape of the
// authorization recognizable at a glance, which is the discipline
// check-row-contains-discipline.sh enforces.
//
// Spelled here rather than borrowed from fixy::fs::CtxAdmitsIoBlock so the
// ledger keeps building while that tree is mid-rewrite. The two are the
// same pair of conjuncts, and re-pointing this at fixy::fs once it is
// green again is a one-line change.
template <class Ctx>
concept CtxFitsLedgerStore = effects::CtxOwnsAllOf<Ctx, effects::Effect::IO, effects::Effect::Block>;

static_assert(CtxFitsLedgerStore<LedgerIoCtx>);
static_assert(CtxFitsLedgerStore<effects::TestRunnerCtx>);
// A foreground context claims nothing, so it cannot open a file.
static_assert(!CtxFitsLedgerStore<effects::HotFgCtx>);
// An initialization context claims IO but not Block, so it cannot wait on
// one either.
static_assert(!CtxFitsLedgerStore<effects::ColdInitCtx>);

// ── Bounds ────────────────────────────────────────────────────────────

// A ledger holds one entry per verdict id, and the id space is small and
// hand-curated. The cap is generous against that and exists so a corrupt or
// hostile file cannot make the reader allocate without limit.
inline constexpr std::size_t kMaxLedgerEntries = 256;

// A ledger that exceeds this is not a ledger. 256 entries at roughly 200
// bytes a line, with headroom.
inline constexpr std::size_t kMaxLedgerFileBytes = 256 * 1024;

inline constexpr std::string_view kLedgerMagic = "crucible-hwledger";
inline constexpr std::uint32_t kLedgerFormatVersion = 1;

using LedgerEntryList = std::inplace_vector<LedgerEntry, kMaxLedgerEntries>;

// ── The in-memory ledger ──────────────────────────────────────────────

struct Ledger {
    HostFingerprint fingerprint{};

    // The competence of the host at the time the file was last written.
    // Kept whole rather than folded so an operator can see what the writer
    // saw without re-deriving it.
    CompetenceReport competence{};

    std::array<char, kVendorBytes> cpu_vendor{};
    std::array<char, kModelBytes> cpu_model{};

    LedgerEntryList entries{};

    [[nodiscard]] const LedgerEntry* find(VerdictId id) const noexcept {
        for (LedgerEntry const& entry : entries) {
            if (entry.id == id) {
                return &entry;
            }
        }
        return nullptr;
    }

    // Replaces an existing verdict of the same id, or appends. Replacing is
    // the common case: a refresh remeasures a question that is already
    // answered, and two answers to one question in one file would make the
    // reader's result depend on scan order.
    [[nodiscard]] std::expected<void, LedgerError> upsert(LedgerEntry const& entry) noexcept {
        for (LedgerEntry& existing : entries) {
            if (existing.id == entry.id) {
                existing = entry;
                return {};
            }
        }
        if (entries.size() >= kMaxLedgerEntries) {
            return std::unexpected(LedgerError::StoreFull);
        }
        entries.push_back(entry);
        return {};
    }
};

// ── Wall clock ────────────────────────────────────────────────────────
//
// system_clock rather than steady_clock, against the house rule, and for
// the reason the house rule allows: a TTL is an age measured across process
// boundaries and reboots, and steady_clock has no epoch two processes can
// agree on. The hazard the rule guards against — a clock jump corrupting a
// measurement — does not apply, because nothing here times anything. A jump
// backwards is handled where it matters, in LedgerEntry::is_expired_at,
// which treats an entry from the future as expired.

[[nodiscard]] inline std::uint64_t wall_clock_unix_seconds() noexcept {
    const auto since_epoch = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch).count();
    if (seconds <= 0) {
        return 0u;  // reads as ClockUnavailable at the admission
    }
    return static_cast<std::uint64_t>(seconds);
}

// ── Paths ─────────────────────────────────────────────────────────────

namespace store_detail {

// The cache root, tainted with its environment provenance so the sanitizer
// has something to launder. An operator who exports XDG_CACHE_HOME=../..
// gets a rejected path rather than a ledger written outside the cache.
[[nodiscard]] inline std::expected<safety::Path<safety::source::Sanitized>, LedgerError>
sanitized_cache_root() noexcept {
    const char* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");

    std::filesystem::path root;
    if (xdg_cache_home != nullptr && xdg_cache_home[0] != '\0') {
        root = std::filesystem::path{xdg_cache_home};
    } else if (home != nullptr && home[0] != '\0') {
        root = std::filesystem::path{home} / ".cache";
    } else {
        return std::unexpected(LedgerError::StorePathUnavailable);
    }
    root /= "crucible";
    root /= "hwledger";

    safety::Path<safety::source::FromEnvPath> tainted{std::move(root)};
    auto sanitized =
        safety::sanitize::path_traversal::sanitize_path_no_dotdot<safety::source::FromEnvPath>(std::move(tainted));
    if (!sanitized.has_value()) {
        return std::unexpected(LedgerError::StorePathUnavailable);
    }
    return std::move(*sanitized);
}

// Both halves in the name. A policy change therefore writes a sibling file
// rather than clobbering one that is still correct under the old setting.
[[nodiscard]] inline std::array<char, 64> ledger_filename(HostFingerprint fingerprint) noexcept {
    std::array<char, 64> name{};
    std::snprintf(name.data(), name.size(), "hw-%016llx-%016llx.ledger",
                  static_cast<unsigned long long>(fingerprint.hardware.raw()),
                  static_cast<unsigned long long>(fingerprint.policy.raw()));
    return name;
}

[[nodiscard]] inline std::array<char, 80> temp_filename(HostFingerprint fingerprint) noexcept {
    std::array<char, 80> name{};
    // The pid keeps two concurrent writers off each other's temporary. Both
    // still race on the rename, and rename(2) settles that: one of the two
    // complete ledgers wins and neither is torn.
    std::snprintf(name.data(), name.size(), "hw-%016llx-%016llx.%d.tmp",
                  static_cast<unsigned long long>(fingerprint.hardware.raw()),
                  static_cast<unsigned long long>(fingerprint.policy.raw()), ::getpid());
    return name;
}

[[nodiscard]] inline std::expected<safety::Path<safety::source::Sanitized>, LedgerError>
under_cache_root(std::string_view leaf) noexcept {
    auto root = sanitized_cache_root();
    if (!root.has_value()) {
        return std::unexpected(root.error());
    }
    std::filesystem::path full = root->value() / std::filesystem::path{leaf};
    safety::Path<safety::source::FromEnvPath> tainted{std::move(full)};
    auto sanitized =
        safety::sanitize::path_traversal::sanitize_path_no_dotdot<safety::source::FromEnvPath>(std::move(tainted));
    if (!sanitized.has_value()) {
        return std::unexpected(LedgerError::StorePathUnavailable);
    }
    return std::move(*sanitized);
}

}  // namespace store_detail

[[nodiscard]] inline std::expected<safety::Path<safety::source::Sanitized>, LedgerError>
ledger_path_for(HostFingerprint fingerprint) noexcept {
    const std::array<char, 64> name = store_detail::ledger_filename(fingerprint);
    return store_detail::under_cache_root(std::string_view{name.data()});
}

// ── Serialization ─────────────────────────────────────────────────────
//
// Tab-separated, one record per line, the record kind in the first field.
// A reader dispatches on that first field and ignores kinds it does not
// know, so a newer writer can add a record type without breaking an older
// reader. Verdicts are written by NAME rather than by numeric id for the
// same reason: an older reader skips a verdict it has never heard of
// instead of misreading it as one it has.

namespace store_detail {

// Fields are appended one at a time rather than formatted in one
// snprintf. Two reasons, and the second is the load-bearing one.
//
// First: the emitted field order becomes a visible sequence in the code,
// which is what the legend line above claims it is. A format string hides
// the order inside a run of %-specifiers that nobody recounts.
//
// Second: a runtime-precision "%.*s" is unbounded as far as the compiler
// can see, so -Wformat-truncation refuses it however small the argument
// actually is. Appending sidesteps the whole class rather than widening a
// buffer until the diagnostic stops.
struct LineBuilder {
    std::string& out;
    bool needs_tab = false;

    void field(std::string_view text) noexcept {
        if (needs_tab) {
            out += '\t';
        }
        out += text;
        needs_tab = true;
    }

    void number(std::uint64_t value) noexcept {
        std::array<char, 24> digits{};
        const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
        field(std::string_view{digits.data(), static_cast<std::size_t>(written.ptr - digits.data())});
    }

    void signed_number(std::int64_t value) noexcept {
        std::array<char, 24> digits{};
        const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
        field(std::string_view{digits.data(), static_cast<std::size_t>(written.ptr - digits.data())});
    }

    // Zero-padded hex, for the two digests and the defect word. Fixed
    // width so a reader can eyeball two fingerprints for equality without
    // counting digits.
    void hex(std::uint64_t value, int width) noexcept {
        std::array<char, 17> digits{};
        const auto written = std::to_chars(digits.data(), digits.data() + digits.size(), value, 16);
        const auto length = static_cast<int>(written.ptr - digits.data());
        if (needs_tab) {
            out += '\t';
            needs_tab = false;
        }
        for (int pad = length; pad < width; ++pad) {
            out += '0';
        }
        out.append(digits.data(), static_cast<std::size_t>(length));
        needs_tab = true;
    }

    void end() noexcept {
        out += '\n';
        needs_tab = false;
    }
};

}  // namespace store_detail

[[nodiscard]] inline std::string serialize_ledger(Ledger const& ledger) noexcept {
    std::string out;
    store_detail::LineBuilder builder{out};

    builder.field(kLedgerMagic);
    builder.number(kLedgerFormatVersion);
    builder.end();

    // A field legend, because the record is positional and the file is
    // meant to be read and occasionally edited by a person. Miscounting a
    // tab-separated column is easy and silent — setting the timestamp when
    // you meant to set the defect word looks like it worked and expires the
    // entry instead. The parser ignores any record kind it does not know,
    // so this line costs nothing to a reader and saves a person a bisect.
    out += "# verdict\tname\tvalue\tunit\tconfidence\tsamples\tp50_ns\tp99_ns\tp999_ns"
           "\tcv_ppm\trun_to_run_ppm\tdefects_hex\tmeasured_at_unix\tttl_s\n";

    builder.field("fingerprint");
    builder.hex(ledger.fingerprint.hardware.raw(), 16);
    builder.hex(ledger.fingerprint.policy.raw(), 16);
    builder.end();

    builder.field("host");
    builder.field(std::string_view{ledger.cpu_vendor.data()});
    builder.field(std::string_view{ledger.cpu_model.data()});
    builder.end();

    std::array<char, 256> defect_text{};
    const std::string_view defects = describe_defects(ledger.competence, defect_text);
    builder.field("competence");
    builder.hex(ledger.competence.defect_word(), 4);
    builder.number(ledger.competence.isolated_core_count);
    builder.number(ledger.competence.online_sibling_count);
    builder.number(ledger.competence.load_average_milli);
    builder.number(ledger.competence.allowed_cpu_count);
    builder.signed_number(ledger.competence.perf_event_paranoid);
    builder.number(ledger.competence.scaling_min_freq_khz);
    builder.number(ledger.competence.scaling_max_freq_khz);
    builder.number(ledger.competence.governor_is_performance ? 1u : 0u);
    builder.field(defects);
    builder.end();

    // The field order here is the legend line above, in the same order.
    // Changing one without the other is the kind of silent skew the
    // legend exists to prevent, so they sit within a screen of each other.
    for (LedgerEntry const& entry : ledger.entries) {
        const VerdictTrait trait = verdict_trait(entry.id);
        builder.field("verdict");
        builder.field(trait.name);
        builder.number(entry.value.value().raw());
        builder.field(verdict_unit_name(trait.unit));
        builder.field(confidence_name(entry.confidence));
        builder.number(entry.evidence.sample_count);
        builder.number(entry.evidence.quantiles.p50_ns);
        builder.number(entry.evidence.quantiles.p99_ns);
        builder.number(entry.evidence.quantiles.p999_ns);
        builder.number(entry.evidence.within_run_cv_ppm);
        builder.number(entry.evidence.run_to_run_spread_ppm);
        builder.hex(entry.competence_defects_at_measurement, 4);
        builder.number(entry.measured_at_unix_seconds);
        builder.number(entry.ttl.seconds);
        builder.end();
    }
    return out;
}

namespace store_detail {

// Splits `text` on `delimiter` into at most `into.size()` fields and
// returns how many were produced. Fields past the cap are dropped rather
// than merged, so a malformed line cannot smuggle content into the last
// field a reader trusts.
[[nodiscard]] inline std::size_t split_fields(std::string_view text, char delimiter,
                                              std::span<std::string_view> into) noexcept {
    std::size_t produced = 0;
    std::size_t cursor = 0;
    while (produced < into.size()) {
        const std::size_t hit = text.find(delimiter, cursor);
        if (hit == std::string_view::npos) {
            into[produced] = text.substr(cursor);
            ++produced;
            break;
        }
        into[produced] = text.substr(cursor, hit - cursor);
        ++produced;
        cursor = hit + 1u;
    }
    return produced;
}

[[nodiscard]] inline std::uint64_t field_unsigned(std::string_view text, int base = 10) noexcept {
    std::uint64_t parsed = 0;
    const auto outcome = std::from_chars(text.data(), text.data() + text.size(), parsed, base);
    return (outcome.ec == std::errc{}) ? parsed : 0u;
}

[[nodiscard]] constexpr Confidence confidence_from_name(std::string_view name) noexcept {
    if (name == "high") {
        return Confidence::High;
    }
    if (name == "low") {
        return Confidence::Low;
    }
    return Confidence::Unknown;
}

}  // namespace store_detail

// Reads a ledger back. Every failure mode collapses to MalformedRecord or a
// dropped line rather than a partially-trusted ledger: a file the reader
// cannot fully understand is a file the runtime should remeasure over.
[[nodiscard]] inline std::expected<Ledger, LedgerError> deserialize_ledger(std::string_view text) noexcept {
    Ledger ledger{};
    bool saw_magic = false;
    bool saw_fingerprint = false;

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t line_end = std::min(text.find('\n', cursor), text.size());
        const std::string_view line = text.substr(cursor, line_end - cursor);
        cursor = line_end + 1u;
        if (line.empty()) {
            continue;
        }

        std::array<std::string_view, 16> field{};
        const std::size_t field_count = store_detail::split_fields(line, '\t', field);
        if (field_count == 0u) {
            continue;
        }

        if (field[0] == kLedgerMagic) {
            if (field_count < 2u || store_detail::field_unsigned(field[1]) != kLedgerFormatVersion) {
                // A version this build does not know is not partially
                // readable. Refusing the whole file forces a remeasure,
                // which is correct and cheap.
                return std::unexpected(LedgerError::MalformedRecord);
            }
            saw_magic = true;
            continue;
        }

        if (field[0] == "fingerprint") {
            if (field_count < 3u) {
                return std::unexpected(LedgerError::MalformedRecord);
            }
            ledger.fingerprint.hardware = HardwareDigest{store_detail::field_unsigned(field[1], 16)};
            ledger.fingerprint.policy = PolicyDigest{store_detail::field_unsigned(field[2], 16)};
            saw_fingerprint = true;
            continue;
        }

        if (field[0] == "host") {
            if (field_count >= 2u) {
                fingerprint_detail::copy_into(ledger.cpu_vendor, field[1]);
            }
            if (field_count >= 3u) {
                fingerprint_detail::copy_into(ledger.cpu_model, field[2]);
            }
            continue;
        }

        if (field[0] == "competence") {
            if (field_count < 10u) {
                return std::unexpected(LedgerError::MalformedRecord);
            }
            ledger.competence.defects = safety::Bits<CompetenceDefect>::from_raw(
                static_cast<std::uint16_t>(store_detail::field_unsigned(field[1], 16)));
            ledger.competence.isolated_core_count = static_cast<std::uint32_t>(store_detail::field_unsigned(field[2]));
            ledger.competence.online_sibling_count = static_cast<std::uint32_t>(store_detail::field_unsigned(field[3]));
            ledger.competence.load_average_milli = static_cast<std::uint32_t>(store_detail::field_unsigned(field[4]));
            ledger.competence.allowed_cpu_count = static_cast<std::uint32_t>(store_detail::field_unsigned(field[5]));
            ledger.competence.perf_event_paranoid = static_cast<std::int32_t>(store_detail::field_unsigned(field[6]));
            ledger.competence.scaling_min_freq_khz = store_detail::field_unsigned(field[7]);
            ledger.competence.scaling_max_freq_khz = store_detail::field_unsigned(field[8]);
            ledger.competence.governor_is_performance = store_detail::field_unsigned(field[9]) != 0u;
            continue;
        }

        if (field[0] == "verdict") {
            if (field_count < 14u) {
                return std::unexpected(LedgerError::MalformedRecord);
            }
            const auto id = verdict_id_from_name(field[1]);
            if (!id.has_value()) {
                // Written by a build that knows more questions than this
                // one. Skipping is the forward-compatible answer; failing
                // would make a fleet mid-upgrade discard every ledger.
                continue;
            }
            const Confidence confidence = store_detail::confidence_from_name(field[4]);
            if (confidence == Confidence::Unknown) {
                // Unknown is never written, so reading one back means the
                // file was edited or corrupted. Dropping the line is
                // fail-closed: the reader ends up with no verdict, which is
                // exactly what an unknown verdict means.
                continue;
            }
            LedgerEntry entry{};
            entry.id = *id;
            entry.value = safety::Tagged<VerdictValue, safety::source::Calibrated>{
                VerdictValue{store_detail::field_unsigned(field[2])}};
            entry.confidence = confidence;
            entry.evidence.sample_count = static_cast<std::uint32_t>(store_detail::field_unsigned(field[5]));
            entry.evidence.quantiles.p50_ns = static_cast<std::uint32_t>(store_detail::field_unsigned(field[6]));
            entry.evidence.quantiles.p99_ns = static_cast<std::uint32_t>(store_detail::field_unsigned(field[7]));
            entry.evidence.quantiles.p999_ns = static_cast<std::uint32_t>(store_detail::field_unsigned(field[8]));
            entry.evidence.within_run_cv_ppm = static_cast<std::uint32_t>(store_detail::field_unsigned(field[9]));
            entry.evidence.run_to_run_spread_ppm = static_cast<std::uint32_t>(store_detail::field_unsigned(field[10]));
            entry.competence_defects_at_measurement =
                static_cast<std::uint16_t>(store_detail::field_unsigned(field[11], 16));
            entry.measured_at_unix_seconds = store_detail::field_unsigned(field[12]);
            entry.ttl = VerdictTtl::of_seconds(static_cast<std::uint32_t>(store_detail::field_unsigned(field[13])));

            // The evidence has to clear the same bar on the way in as it did
            // on the way out. Without this an edited file could promote a
            // verdict to high confidence by changing one word, and the
            // reader would serve it.
            if (derive_confidence(entry.evidence, ledger.competence) == Confidence::Unknown) {
                continue;
            }
            // A stored grade may not exceed what the recorded evidence and
            // the recorded competence support. It may be lower — a writer is
            // free to be more cautious than the rule.
            const Confidence supported =
                (entry.competence_defects_at_measurement == 0u) ? Confidence::High : Confidence::Low;
            if (entry.confidence > supported) {
                continue;
            }
            auto placed = ledger.upsert(entry);
            if (!placed.has_value()) {
                return std::unexpected(placed.error());
            }
            continue;
        }

        // An unknown record kind from a newer writer. Ignored on purpose.
    }

    if (!saw_magic || !saw_fingerprint) {
        return std::unexpected(LedgerError::MalformedRecord);
    }
    return ledger;
}

// ── Load and commit ───────────────────────────────────────────────────

namespace store_detail {

// The one direct syscall in this header. fsync has no typed wrapper in the
// tree, and the alternative — skipping it — would let the rename publish a
// filename whose contents are still in page cache, so a crash leaves the
// target pointing at a truncated or empty inode. That is strictly worse
// than pointing at the previous ledger, which is what the whole
// tmp-then-rename shape exists to guarantee.
[[nodiscard]] inline bool flush_descriptor(safety::FileHandle const& file) noexcept {
    if (!file.is_open()) {
        return false;
    }
    // The capability proof for the call below: the only caller is
    // commit_ledger, whose requires-clause is CtxFitsLedgerStore<Ctx> —
    // effects::IO plus effects::Block, checked at the type level and
    // witnessed by the static_asserts above that reject HotFgCtx (no
    // capability at all) and ColdInitCtx (IO but not Block).
    while (true) {
        const int outcome = ::fsync(file.get());  // SYSCALL-CAP-OK: see the proof above
        if (outcome == 0) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
}

}  // namespace store_detail

// §XXI carve-out: cx=alloc — reading a file invokes the kernel, so this
// cannot be constexpr and is not marked so.
template <effects::IsExecCtx Ctx>
    requires CtxFitsLedgerStore<Ctx>
[[nodiscard]] inline std::expected<Ledger, LedgerError> load_ledger(Ctx const& /* ctx */,
                                                                    HostFingerprint fingerprint) noexcept {
    auto path = ledger_path_for(fingerprint);
    if (!path.has_value()) {
        return std::unexpected(path.error());
    }

    auto opened = safety::open_read(path->value().c_str());
    if (!opened.has_value()) {
        return std::unexpected(LedgerError::StoreReadFailed);
    }

    safety::FileHandle const& file = *opened;
    auto size = safety::file_size(file);
    if (!size.has_value() || *size < 0) {
        return std::unexpected(LedgerError::StoreReadFailed);
    }
    const std::size_t byte_count = static_cast<std::size_t>(*size);
    if (byte_count == 0u || byte_count > kMaxLedgerFileBytes) {
        return std::unexpected(LedgerError::MalformedRecord);
    }

    std::string buffer(byte_count, '\0');
    auto filled = safety::read_full(file, std::as_writable_bytes(std::span<char>{buffer.data(), buffer.size()}));
    if (!filled.has_value()) {
        return std::unexpected(LedgerError::StoreReadFailed);
    }
    buffer.resize(*filled);

    auto parsed = deserialize_ledger(buffer);
    if (!parsed.has_value()) {
        return std::unexpected(parsed.error());
    }
    // The filename says one fingerprint and the record inside says another.
    // That is a file someone moved or hand-edited, and trusting the content
    // over the name would serve verdicts from a different machine.
    if (parsed->fingerprint != fingerprint) {
        return std::unexpected(LedgerError::FingerprintMismatch);
    }
    return parsed;
}

// §XXI carve-out: cx=alloc — writing a file invokes the kernel.
template <effects::IsExecCtx Ctx>
    requires CtxFitsLedgerStore<Ctx>
[[nodiscard]] inline std::expected<void, LedgerError> commit_ledger(Ctx const& /* ctx */,
                                                                    Ledger const& ledger) noexcept {
    if (!ledger.fingerprint.is_complete()) {
        return std::unexpected(LedgerError::FingerprintMismatch);
    }

    auto root = store_detail::sanitized_cache_root();
    if (!root.has_value()) {
        return std::unexpected(root.error());
    }
    std::error_code created{};
    std::filesystem::create_directories(root->value(), created);
    if (created) {
        return std::unexpected(LedgerError::StorePathUnavailable);
    }

    const std::array<char, 80> temp_name = store_detail::temp_filename(ledger.fingerprint);
    auto temp_path = store_detail::under_cache_root(std::string_view{temp_name.data()});
    if (!temp_path.has_value()) {
        return std::unexpected(temp_path.error());
    }
    auto target_path = ledger_path_for(ledger.fingerprint);
    if (!target_path.has_value()) {
        return std::unexpected(target_path.error());
    }

    const std::string body = serialize_ledger(ledger);

    {
        auto opened = safety::open_write_truncate(temp_path->value().c_str(), 0644);
        if (!opened.has_value()) {
            return std::unexpected(LedgerError::StoreWriteFailed);
        }
        safety::FileHandle const& file = *opened;
        auto written = safety::write_full(file, std::as_bytes(std::span<const char>{body.data(), body.size()}));
        if (!written.has_value()) {
            return std::unexpected(LedgerError::StoreWriteFailed);
        }
        // The sync happens while the descriptor is open, and the rename
        // happens after the sync. Reversing them would let a crash publish
        // the target name over an inode whose data never reached disk.
        if (!store_detail::flush_descriptor(file)) {
            return std::unexpected(LedgerError::StoreWriteFailed);
        }
    }

    // std::filesystem::rename with an error_code is rename(2) underneath and
    // does not throw. Both paths sit in the same directory, so the rename is
    // within one filesystem and is therefore atomic.
    //
    // The parent directory is deliberately NOT synced afterwards. That sync
    // buys durability of the rename across a power cut, and this is a cache:
    // the cost of losing the last commit is one remeasure. Paying a second
    // fsync on every write to avoid that is the wrong trade for a mechanism
    // whose stated constraint is lightweight.
    std::error_code renamed{};
    std::filesystem::rename(temp_path->value(), target_path->value(), renamed);
    if (renamed) {
        // Leaving the temporary behind on a failed rename would accumulate
        // one file per failed commit, so it goes even though the commit did
        // not.
        std::error_code ignored{};
        std::filesystem::remove(temp_path->value(), ignored);
        return std::unexpected(LedgerError::StoreCommitFailed);
    }
    return {};
}

namespace store_detail::self_test {

static_assert(kMaxLedgerEntries >= kVerdictIdCount);
static_assert(confidence_from_name("high") == Confidence::High);
static_assert(confidence_from_name("low") == Confidence::Low);
static_assert(confidence_from_name("unknown") == Confidence::Unknown);
static_assert(confidence_from_name("HIGH") == Confidence::Unknown, "the parse is exact, not case-folding");
static_assert(confidence_from_name("") == Confidence::Unknown);

// Confidence must order Unknown < Low < High for the read-back clamp in
// deserialize_ledger to mean what it says.
static_assert(Confidence::Unknown < Confidence::Low);
static_assert(Confidence::Low < Confidence::High);

}  // namespace store_detail::self_test

}  // namespace crucible::ledger
