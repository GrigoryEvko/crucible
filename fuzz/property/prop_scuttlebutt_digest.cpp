// ═══════════════════════════════════════════════════════════════════
// prop_scuttlebutt_digest.cpp — upsert-semantics fuzzer for the canopy
// Scuttlebutt anti-entropy digest / request-set (canopy/Scuttlebutt.h).
//
// ScuttlebuttDigest::push and ScuttlebuttRequestSet::push are the
// foundational bookkeeping for canopy's gossip reconciliation: each is a
// bounded set of (origin, key) → version cells where push keeps the MAX
// version per distinct (origin, key), rejects malformed entries, and
// caps total distinct cells at capacity = MaxPeers × MaxKeys.  If push
// ever kept a stale (lower) version, admitted a duplicate, or mishandled
// the capacity bound, a peer's digest would advertise wrong versions and
// the reconciliation diff (compare_digest) would send/withhold the wrong
// deltas — silently diverging the fleet.  test_scuttlebutt pins
// hand-picked cases; no property fuzzer existed.
//
// The two pushes differ in ONE load-bearing way that this fuzzer pins:
//   • Digest::push: version==0 is checked FIRST and returns true (a
//     silent no-op skip), even for an otherwise-malformed entry.
//   • RequestSet::push: version==0 is folded into the malformed gate and
//     returns false.
// A refactor that unified them would break one direction; the asymmetry
// is asserted per-push.
//
// The INDEPENDENT oracle is a flat reference map (capacity cells of
// (origin_idx, key_idx, version)) that I maintain myself as I replay the
// push sequence — a different representation from the production
// FixedArray, with the dedup/max/capacity/zero rules transcribed against
// MY map, not production's.  Per push it asserts the return value
// matches the oracle's prediction; after the sequence it asserts the
// structure's entry SET equals the reference map (order-independent), and
// — for the digest — that well_formed() holds (deduped, all version>0,
// count≤capacity) as an extra structural witness.
//
// MaxPeers=2, MaxKeys=2 → capacity 4, but the (origin,key) universe is
// 3×3=9 distinct cells, so the capacity-full reject path (and the
// "update existing succeeds even when full" nuance) both fire.  Versions
// are corner-biased toward {0, 1, UINT64_MAX} so the max-keeping and the
// version==0 asymmetry are exercised densely; ~1/6 of entries are
// malformed (zero origin / zero key hash / zero key length) to drive the
// reject + ordering paths.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/canopy/Scuttlebutt.h>
#include <crucible/cog/CogIdentity.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

namespace cc = crucible::canopy;
using crucible::fuzz::prop::Rng;
using Uuid = crucible::cog::Uuid;

using Dig = cc::ScuttlebuttDigest<2, 2>;
using Req = cc::ScuttlebuttRequestSet<2, 2>;
using Entry = cc::ScuttlebuttVersionEntry;
using Key = cc::ScuttlebuttKey;

inline constexpr std::size_t kCap = Dig::capacity;   // MaxPeers*MaxKeys = 4
inline constexpr std::uint32_t kOrigins = 3;         // 3×3 universe > cap → overflow
inline constexpr std::uint32_t kKeys = 3;
inline constexpr std::uint32_t kMaxPushes = 16;
inline constexpr std::uint64_t kU64Max =
    std::numeric_limits<std::uint64_t>::max();

// kind: 0 = valid; 1 = zero origin; 2 = zero key hash; 3 = zero key length.
struct EntrySpec {
    std::uint32_t oidx = 0;
    std::uint32_t kidx = 0;
    std::uint64_t version = 0;
    std::uint8_t kind = 0;
};
struct Spec {
    std::array<EntrySpec, kMaxPushes> e{};
    std::uint32_t count = 0;
};

[[nodiscard]] std::uint64_t gen_version(Rng& rng) noexcept {
    switch (rng.next_below(5u)) {
        case 0: return 0u;                  // exercises the digest/request asymmetry
        case 1: return 1u;
        case 2: return 1u + rng.next_below(100u);
        case 3: return kU64Max;
        default: return rng.next64();
    }
}

[[nodiscard]] std::uint8_t gen_kind(Rng& rng) noexcept {
    // ~1/6 malformed, split across the three malformation classes.
    return rng.next_below(6u) == 0u
        ? static_cast<std::uint8_t>(1u + rng.next_below(3u))
        : std::uint8_t{0};
}

[[nodiscard]] Entry make_entry(const EntrySpec& s) noexcept {
    const Uuid origin = s.kind == 1u
        ? Uuid{}
        : Uuid{static_cast<std::uint64_t>(s.oidx) + 1u,
               static_cast<std::uint64_t>(s.oidx) + 101u};
    const std::uint64_t hash =
        s.kind == 2u ? 0u : static_cast<std::uint64_t>(s.kidx) + 1u;
    const std::uint16_t length =
        s.kind == 3u ? std::uint16_t{0} : std::uint16_t{1};
    return Entry{.origin = origin,
                 .key = Key{.hash = hash, .length = length},
                 .version = s.version};
}

// ─── independent reference map (flat cells, max version per key) ────

struct Cell {
    std::uint32_t oidx = 0;
    std::uint32_t kidx = 0;
    std::uint64_t version = 0;
};
struct Ref {
    std::array<Cell, kCap> c{};
    std::size_t n = 0;
};

// Replays one push against the reference map and returns the predicted
// push result.  is_digest selects the version==0 policy (skip→true vs
// reject→false).
[[nodiscard]] bool step(Ref& ref, const EntrySpec& s, bool is_digest) noexcept {
    const bool malformed = s.kind != 0u;
    if (is_digest) {
        if (s.version == 0u) return true;       // silent skip, no state change
        if (malformed) return false;
    } else {
        if (s.version == 0u || malformed) return false;
    }
    for (std::size_t i = 0; i < ref.n; ++i) {
        if (ref.c[i].oidx == s.oidx && ref.c[i].kidx == s.kidx) {
            if (ref.c[i].version < s.version) ref.c[i].version = s.version;
            return true;                        // update existing (even when full)
        }
    }
    if (ref.n == kCap) return false;            // distinct-cell overflow
    ref.c[ref.n] = Cell{s.oidx, s.kidx, s.version};
    ++ref.n;
    return true;
}

[[nodiscard]] bool match_entry(const Ref& ref, const Entry& got) noexcept {
    for (std::size_t i = 0; i < ref.n; ++i) {
        const Uuid expect{static_cast<std::uint64_t>(ref.c[i].oidx) + 1u,
                          static_cast<std::uint64_t>(ref.c[i].oidx) + 101u};
        const std::uint64_t expect_hash =
            static_cast<std::uint64_t>(ref.c[i].kidx) + 1u;
        if (got.origin == expect && got.key.hash == expect_hash &&
            got.key.length == std::uint16_t{1} &&
            got.version == ref.c[i].version) {
            return true;
        }
    }
    return false;
}

template <typename Structure>
[[nodiscard]] bool set_eq(const Structure& s, const Ref& ref) noexcept {
    if (static_cast<std::size_t>(s.count) != ref.n) return false;
    for (std::uint16_t i = 0; i < s.count; ++i) {
        if (!match_entry(ref, s.entries[static_cast<std::size_t>(i)])) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;

    return run("scuttlebutt_push", cfg,
        [](Rng& rng) noexcept -> Spec {
            Spec spec{};
            spec.count = 1u + rng.next_below(kMaxPushes - 1u);
            for (std::uint32_t i = 0; i < spec.count; ++i) {
                spec.e[i] = EntrySpec{
                    .oidx = rng.next_below(kOrigins),
                    .kidx = rng.next_below(kKeys),
                    .version = gen_version(rng),
                    .kind = gen_kind(rng),
                };
            }
            return spec;
        },
        [](const Spec& spec) noexcept -> bool {
            Dig dig{};
            Req req{};
            Ref rd{};
            Ref rr{};

            for (std::uint32_t i = 0; i < spec.count; ++i) {
                const EntrySpec& s = spec.e[i];
                const Entry e = make_entry(s);

                if (dig.push(e) != step(rd, s, /*is_digest=*/true)) return false;
                if (req.push(e) != step(rr, s, /*is_digest=*/false)) return false;
            }

            // Final entry sets match the independently-maintained maps.
            if (!set_eq(dig, rd)) return false;
            if (!set_eq(req, rr)) return false;

            // Digest structural invariant (deduped, all version>0, ≤cap).
            if (!dig.well_formed()) return false;

            return true;
        });
}
