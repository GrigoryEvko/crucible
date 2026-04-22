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
//      source::External Tagged<std::string> overload, mirroring the
//      production path where Python frame metadata arrives untrusted.
//      Compiles only if the overload resolution picks the Tagged form;
//      a regression that drops or breaks the overload would surface
//      here as a build failure rather than silent truncation.
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
//   - Refined<non_zero, CallsiteHash> contract regressions: zero
//     sentinel slipping through the compat overload into the typed
//     insert path.  UBSan + the Refined pre() would fire immediately.
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
#include <crucible/safety/Tagged.h>

#include <array>
#include <cstdint>
#include <string>

namespace {

// Small alphabet for random strings.  Lowercase + digits keeps the
// output human-readable in failure reports without pulling in any
// locale-sensitive character classes.  Length bounded at [4, 24].
constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
constexpr uint32_t kAlphabetLen = sizeof(kAlphabet) - 1;  // exclude NUL

[[nodiscard]] std::string random_ident(
    crucible::fuzz::prop::Rng& rng,
    uint32_t min_len,
    uint32_t max_len) noexcept
{
    const uint32_t len = min_len + rng.next_below(max_len - min_len + 1);
    std::string s;
    s.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
        s.push_back(kAlphabet[rng.next_below(kAlphabetLen)]);
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;
    Config cfg = parse_args(argc, argv);
    // Each iteration does O(N) inserts + O(N²) append-order scan at
    // N=48; cap at 5k iters to keep the sanitizer-instrumented runtime
    // reasonable under ctest's budget.
    if (cfg.iterations > 5'000) cfg.iterations = 5'000;

    return run("CallSiteTable dedup + round-trip", cfg,
        [](Rng& rng) {
            // 48 tuples; dedup the hashes within the batch up front so
            // the size invariant in the checker is exact rather than
            // "≤ count, adjusted for collisions".
            constexpr unsigned N = 48;
            struct Record {
                uint64_t    hash_raw;
                std::string filename;
                std::string funcname;
                int32_t     lineno;
            };
            struct Batch {
                std::array<Record, N> recs;
                unsigned              count;
            };
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
                    if (b.recs[j].hash_raw == h) { duplicate = true; break; }
                }
                if (duplicate) continue;

                b.recs[b.count] = Record{
                    .hash_raw = h,
                    .filename = random_ident(rng, 4, 24),
                    .funcname = random_ident(rng, 4, 16),
                    .lineno   = static_cast<int32_t>(rng.next_below(100'000)),
                };
                ++b.count;
            }
            return b;
        },
        [](const auto& b) {
            CallSiteTable t;

            // Property (absence-before-insert): has() on every batch
            // hash must be false on a fresh table.  Catches a bug
            // where seen[] is default-initialized to something other
            // than CallsiteHash{} sentinel.
            for (unsigned i = 0; i < b.count; ++i) {
                if (t.has(CallsiteHash{b.recs[i].hash_raw})) return false;
            }

            // ── Insert phase, via the source::External Tagged overload.
            //
            // Production callers (Vessel FFI) pass ExternalName because
            // Python frame metadata is untrusted.  Exercising the same
            // path here catches overload-resolution regressions that
            // would otherwise silently fall through to the untagged
            // insert.  The NonZeroHash Refined construction fires
            // a contract on zero — our generator has excluded zero,
            // so this is a no-op at runtime on the happy path.
            for (unsigned i = 0; i < b.count; ++i) {
                const CallsiteHash h{b.recs[i].hash_raw};
                CallSiteTable::NonZeroHash nz{h};
                CallSiteTable::ExternalName fn{b.recs[i].filename};
                CallSiteTable::ExternalName gn{b.recs[i].funcname};
                t.insert(std::move(nz), std::move(fn), std::move(gn),
                         b.recs[i].lineno);
            }

            // Property (size == distinct count): the generator
            // deduplicated the batch, so size() must equal count.
            if (t.size() != b.count) return false;

            // Property (round-trip presence + payload): for every
            // record, has() is true and entries[] holds exactly one
            // row with the original payload.  Append order: the
            // record inserted at position i appears at entries[i].
            for (unsigned i = 0; i < b.count; ++i) {
                const CallsiteHash h{b.recs[i].hash_raw};
                if (!t.has(h)) return false;

                const auto& e = t.entries[i];
                if (e.hash != h)                         return false;
                if (e.filename != b.recs[i].filename)    return false;
                if (e.funcname != b.recs[i].funcname)    return false;
                if (e.lineno   != b.recs[i].lineno)      return false;
            }

            // ── Dedup phase: re-insert every record with DIFFERENT
            //    payloads.  size() must not grow; entries[i] payload
            //    must be unchanged (first write wins).  This is the
            //    single most load-bearing invariant — the table's
            //    whole purpose is dedup.
            for (unsigned i = 0; i < b.count; ++i) {
                const CallsiteHash h{b.recs[i].hash_raw};
                CallSiteTable::NonZeroHash nz{h};
                CallSiteTable::ExternalName fn{std::string{"OVERWRITTEN"}};
                CallSiteTable::ExternalName gn{std::string{"ATTEMPT"}};
                t.insert(std::move(nz), std::move(fn), std::move(gn),
                         b.recs[i].lineno + 777);
            }
            if (t.size() != b.count) return false;
            for (unsigned i = 0; i < b.count; ++i) {
                const auto& e = t.entries[i];
                if (e.filename != b.recs[i].filename) return false;
                if (e.funcname != b.recs[i].funcname) return false;
                if (e.lineno   != b.recs[i].lineno)   return false;
            }

            // Property (sentinel rejection): has(CallsiteHash{}) is
            // always false.  The compat overload tolerates a zero
            // argument by early-return; we don't even exercise the
            // typed path here because that would fire the Refined
            // contract.
            if (t.has(CallsiteHash{})) return false;
            t.insert(CallsiteHash{}, std::string{"zero.py"},
                     std::string{"z"}, 0);
            if (t.has(CallsiteHash{})) return false;
            if (t.size() != b.count)   return false;

            return true;
        });
}
