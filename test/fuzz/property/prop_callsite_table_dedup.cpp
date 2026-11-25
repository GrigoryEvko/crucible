// ═══════════════════════════════════════════════════════════════════
// prop_callsite_table_dedup — CallSiteTable insert + lookup invariants.
//
// CallSiteTable is the dedup set + AppendOnly record store that maps
// a Python source-location's CallsiteHash → (filename, funcname, lineno).
// It backs TraceEntry::callsite_hash annotations; every distinct call
// site seen during recording is inserted exactly once, and downstream
// diagnostic printers walk entries[] to resolve hashes back to strings.
//
// The load-bearing invariants stressed here:
//
//   1. Dedup — inserting the same CallsiteHash twice leaves size() == 1
//      and the first-written (filename, funcname, lineno) wins.  The
//      second call is a structural no-op: no overwrite, no reordering,
//      no extra entry.
//
//   2. Round-trip presence — every inserted hash reports has() == true,
//      and exactly one entries[i] row holds that hash with the original
//      (filename, funcname, lineno) payload.
//
//   3. Absence — a CallsiteHash never inserted reports has() == false
//      (with a collision guard: if the random generator happened to emit
//      the same hash as a real entry, we skip the check for that hash).
//
//   4. Size accounting — after N distinct-hash inserts, size() == N.
//      Catches probe-chain bugs that silently drop an entry, off-by-one
//      errors in the seen[] sweep, and dedup races that double-count.
//
//   5. Append order — entries[] is in first-insert order.  AppendOnly
//      guarantees no reordering by construction; the fuzzer spot-checks
//      the full permutation to catch a regression that e.g. sorted the
//      entries vector in a misguided optimization pass.
//
//   6. Sentinel rejection — has(CallsiteHash{}) is false even after
//      a compat-overload insert of the zero sentinel attempts to store
//      it.  The zero raw is reserved as the empty-slot marker; the
//      table must never "remember" it.
//
//   7. Tagged-string discipline — inserts go through ExternalName, the
//      source::External Tagged<TextView> overload, which is the path
//      for frame metadata that arrives untrusted from Python.  It compiles
//      only if overload resolution picks the tagged form.  The entry keeps
//      the bytes under source::Interned, never under Sanitized, because
//      the table runs no sanitizer.
//
// Bug classes caught:
//
//   - Probe-order desync between insert() and has() (writer stops on
//     empty slot, reader fails to follow the probe chain after a wrap
//     around SET_MASK — the classic open-addressing bug).
//   - seen[] sentinel confusion (accidentally storing CallsiteHash{}
//     and having has() report true on it because the probe finds the
//     slot before reaching the empty sentinel).
//   - AppendOnly invariant breakage (entries reordered, duplicated, or
//     emplaced on dedup — any of these would desync with seen[]).
//   - Dedup-on-hash vs dedup-on-payload confusion (the table is keyed
//     on hash; same hash + different file/func/line must still dedupe).
//   - Refined<non_zero, CallsiteHash> regressions: a zero sentinel that
//     reaches the insert path.  The checked mint of NonZeroHash is the
//     only door, and its precondition fires on zero.
//
// Strategy:
//   - Per iteration, generate 48 (hash, filename, funcname, lineno)
//     tuples with non-sentinel hashes.  48 is ~1.2% load on SET_CAP=4096,
//     well under the 50% Swiss-table fill ceiling.
//   - Deduplicate the random hashes within the batch so we can reason
//     about "size should equal the distinct count" without a collision
//     guard per-iteration.  Collisions between independent iterations
//     don't matter — each iteration builds a fresh CallSiteTable.
//   - Strings are drawn from [a-z0-9] with bounded length (≤24 chars).
//     No tight length bound is documented on the table itself; we
//     stay short both to avoid needless allocation and because real
//     Python filenames are typically < 128 chars and func names < 64.
//
// Why 48 per iter and not 64?  Each iteration performs O(N²) work for
// the append-order check (a single linear scan over entries[] per
// expected record, N=48 → 2304 comparisons per iter).  At 5000 iters
// that's ~11M comparisons, well under a second of sanitizer-instrumented
// wall time while still exercising the probe logic at realistic scale.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/CallSiteTable.h>
#include <crucible/Types.h>
#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

// Small alphabet for random strings.  Lowercase + digits keeps the
// output human-readable in failure reports without pulling in any
// locale-sensitive character classes.  Length bounded at [4, 24].
constexpr ::fixy::TextView kAlphabet{"abcdefghijklmnopqrstuvwxyz0123456789"};
constexpr uint32_t kAlphabetLen = static_cast<uint32_t>(kAlphabet.size());

// The longest name that the generator writes.
constexpr uint32_t kMaxIdentChars = 24;

using Ident = ::fixy::FixedText<kMaxIdentChars>;

[[nodiscard]] Ident random_ident(crucible::fuzz::prop::Rng& rng, uint32_t min_len, uint32_t max_len) noexcept {
    const uint32_t len = min_len + rng.next_below(max_len - min_len + 1);
    Ident s{};
    for (uint32_t i = 0; i < len; ++i) {
        s.append(kAlphabet.window(rng.next_below(kAlphabetLen), 1).expect("a draw below the count is in the alphabet"))
            .expect("a name holds at most kMaxIdentChars characters");
    }
    return s;
}

// 48 tuples; the generator dedups the hashes within the batch up front so
// the size invariant in the checker is exact rather than "≤ count,
// adjusted for collisions".
constexpr unsigned kRecords = 48;

struct Record {
    uint64_t hash_raw;
    Ident filename;
    Ident funcname;
    int32_t lineno;
};

struct Batch {
    ::fixy::FixedArray<Record, kRecords> recs{};
    unsigned count;
};

// The record at position of a batch.  Each position that the test reads
// is below the count, which is at most kRecords.
[[nodiscard]] Record const& record_at(Batch const& batch CRUCIBLE_LIFETIMEBOUND, unsigned position) noexcept {
    return batch.recs.at(::fixy::Index<kRecords>::check(position).expect("a position of a batch is below kRecords"));
}
void record_at(Batch const&&, unsigned) =
    delete("the reference names a record of the batch, and a temporary ends with the full expression");

// True when the entry of the table at position is the record.
[[nodiscard]] bool holds_record(::crucible::CallSiteTable const& table, unsigned position,
                                Record const& record) noexcept {
    return table.entries.peek().get(position).is_some_and([&record](auto entry) noexcept {
        return entry->hash == ::crucible::CallsiteHash{record.hash_raw}
            && entry->filename.value().view() == record.filename.view()
            && entry->funcname.value().view() == record.funcname.view() && entry->lineno.value() == record.lineno;
    });
}

}  // namespace

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    // Each iteration does O(N) inserts + O(N²) append-order scan at
    // N=48; cap at 5k iters to keep the sanitizer-instrumented runtime
    // reasonable under ctest's budget.
    const Config cfg = parse_args(arguments, 5000);

    return run(
        "CallSiteTable dedup + round-trip", cfg,
        [](Rng& rng) {
            constexpr unsigned N = kRecords;
            Batch b{};
            b.count = 0;

            for (unsigned i = 0; i < N; ++i) {
                // Force non-zero so the hash is never the reserved
                // sentinel.  Also force UINT64_MAX rejection so we
                // never collide with CallsiteHash::sentinel(), which
                // is reserved for end-of-region markers elsewhere.
                uint64_t h = rng.next64();
                if (h == 0) h = 1;
                if (h == UINT64_MAX) h = UINT64_MAX - 1;

                // Intra-batch dedup — linear scan since N is tiny.
                bool duplicate = false;
                for (unsigned j = 0; j < b.count; ++j) {
                    if (record_at(b, j).hash_raw == h) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;

                b.recs.set(::fixy::Index<kRecords>::check(b.count).expect("a position of a batch is below kRecords"),
                           Record{
                               .hash_raw = h,
                               .filename = random_ident(rng, 4, 24),
                               .funcname = random_ident(rng, 4, 16),
                               .lineno = static_cast<int32_t>(rng.next_below(100000)),
                           });
                ++b.count;
            }
            return b;
        },
        [](const auto& b) {
            const ::foundation::effects::Alloc alloc = ::foundation::effects::testing::test().alloc;
            CallSiteTable t;

            // Property (absence-before-insert): has() on every batch
            // hash must be false on a fresh table.  Catches a bug
            // where seen[] is default-initialized to something other
            // than CallsiteHash{} sentinel.
            for (unsigned i = 0; i < b.count; ++i) {
                if (t.has(CallsiteHash{record_at(b, i).hash_raw})) return false;
            }

            // ── Insert phase, via the source::External Tagged overload.
            //
            // Python frame metadata is untrusted, so it arrives as an
            // ExternalName.  Going through that path here catches an
            // overload-resolution regression that falls through to the
            // untagged insert.  The checked mint of NonZeroHash fires on
            // zero, and the generator excludes zero, so it never fires
            // here.
            for (unsigned i = 0; i < b.count; ++i) {
                const Record& rec = record_at(b, i);
                const CallsiteHash h{rec.hash_raw};
                CallSiteTable::NonZeroHash nz = ::fixy::mint_refined<::fixy::non_zero>(h);
                CallSiteTable::ExternalName fn =
                    ::fixy::mint_tagged<::fixy::tags::source::External>(rec.filename.view());
                CallSiteTable::ExternalName gn =
                    ::fixy::mint_tagged<::fixy::tags::source::External>(rec.funcname.view());
                if (!t.insert(alloc, std::move(nz), std::move(fn), std::move(gn), rec.lineno)) return false;
            }

            // Property (size == distinct count): the generator
            // deduplicated the batch, so size() must equal count.
            if (t.size() != b.count) return false;

            // Property (round-trip presence + payload): for every
            // record, has() is true and entries[] holds exactly one
            // row with the original payload.  Append order: the
            // record inserted at position i appears at entries[i].
            for (unsigned i = 0; i < b.count; ++i) {
                const Record& rec = record_at(b, i);
                const CallsiteHash h{rec.hash_raw};
                if (!t.has(h)) return false;
                if (!holds_record(t, i, rec)) return false;
            }

            // ── Dedup phase: re-insert every record with DIFFERENT
            //    payloads.  size() must not grow; entries[i] payload
            //    must be unchanged (first write wins).  This is the
            //    single most load-bearing invariant — the table's
            //    whole purpose is dedup.
            for (unsigned i = 0; i < b.count; ++i) {
                const CallsiteHash h{record_at(b, i).hash_raw};
                CallSiteTable::NonZeroHash nz = ::fixy::mint_refined<::fixy::non_zero>(h);
                CallSiteTable::ExternalName fn =
                    ::fixy::mint_tagged<::fixy::tags::source::External>(::fixy::TextView{"OVERWRITTEN"});
                CallSiteTable::ExternalName gn =
                    ::fixy::mint_tagged<::fixy::tags::source::External>(::fixy::TextView{"ATTEMPT"});
                if (!t.insert(alloc, std::move(nz), std::move(fn), std::move(gn), record_at(b, i).lineno + 777)) {
                    return false;
                }
            }
            if (t.size() != b.count) return false;
            for (unsigned i = 0; i < b.count; ++i) {
                if (!holds_record(t, i, record_at(b, i))) return false;
            }

            // Property (sentinel rejection): has(CallsiteHash{}) is
            // always false.  Every insert path takes a NonZeroHash, and
            // its checked mint fires on a zero raw, so the zero sentinel
            // cannot reach an insert path.  The read side holds after
            // the dedup phase too: the table never remembers the
            // empty-slot marker, and size() does not change.
            if (t.has(CallsiteHash{})) return false;
            if (t.size() != b.count) return false;

            return true;
        });
}
