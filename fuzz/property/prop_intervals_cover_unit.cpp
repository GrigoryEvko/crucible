// ═══════════════════════════════════════════════════════════════════
// prop_intervals_cover_unit.cpp — differential-oracle fuzzer for
// decide::intervals_cover_unit (safety/Decide.h).
//
// intervals_cover_unit returns true iff a family of half-open
// intervals forms an EXACT PARTITION of [0, total): well-formed,
// non-empty, contained, pairwise-disjoint, and width-sum == total
// (no gaps, no overlaps).  It is the CONTRACT-119 gate (Cipher
// cold-tier blob layout must hole-free tile the on-disk blob) and the
// CONTRACT-110 gate (5D parallelism shards must exactly partition each
// tensor dim).  A false-accept ships a blob layout with a hole or a
// shard cover with a gap; a false-reject blocks a valid plan.  Its
// O(n²)-disjoint + width-sum logic is the same shape that yielded a
// real empty-interval bug in its sibling intervals_pairwise_disjoint
// (fixed in ab7ce9bf), so it is worth fuzzing before its production
// cites land.  Today it has only hand-picked static_assert coverage.
//
// The oracle is an INDEPENDENT algorithm: a coverage BITMAP.  It marks
// every integer of [0, total) once per covering interval, rejecting on
// the first double-mark (overlap) or any malformed/empty/out-of-bounds
// interval, then scans for an unmarked integer (gap).  That is a
// different computation from the code-under-test (which never
// materializes the covered set — it reasons via pairwise disjointness
// + a width sum), so a disagreement on any input is a genuine bug in
// one of them, not a re-derivation of the same arithmetic.
//
// Five generator modes give teeth-by-construction:
//   * ExactPartition — chop [0, total) into consecutive non-empty
//     pieces (optionally shuffled, since the predicate is order-
//     independent).  MUST return true.
//   * WithGap        — a partition with one piece deleted.  The
//     deleted piece's integers are now uncovered.  MUST return false.
//   * WithOverlap    — a partition with one piece duplicated.  Its
//     integers are double-covered.  MUST return false.
//   * WithEmpty      — a partition with an empty [k, k) inserted.
//     cover_unit rejects empties (its semantic note 2).  MUST return
//     false.
//   * Random         — arbitrary intervals; the oracle is ground
//     truth and the code-under-test must match it.
//
// Every iteration asserts the UNIVERSAL differential (cut == oracle)
// AND, for the four constructed modes, the direction the construction
// guarantees.  ExactPartition (true) and WithGap/WithOverlap/WithEmpty
// (false) are opposite-direction, so a vacuous oracle cannot satisfy
// the whole suite — passing all modes is itself the proof that the
// predicate genuinely distinguishes a partition from a non-partition.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"

#include <crucible/safety/Decide.h>

#include <array>
#include <cstdint>
#include <span>
#include <utility>

namespace {

// Bounds: total ≤ 256 keeps the O(total) bitmap oracle cheap under
// ASan; up to 64 pieces exercises dense many-piece partitions while
// leaving 2 array slots for the duplicate/insert mutations.
inline constexpr uint32_t kMaxTotal = 256;
inline constexpr uint32_t kMaxIvs   = 64;

using Iv = crucible::decide::Interval<uint64_t>;

enum class Mode : uint8_t {
    ExactPartition = 0,
    WithGap        = 1,
    WithOverlap    = 2,
    WithEmpty      = 3,
    Random         = 4,
};

struct CoverSpec {
    std::array<Iv, kMaxIvs + 2> ivs{};
    uint64_t total = 0;
    uint32_t count = 0;
    Mode     mode  = Mode::Random;
    uint8_t  pad[3]{};
};

using crucible::fuzz::prop::Rng;

// Independent oracle: mark-and-scan coverage bitmap.  Replicates the
// SPECIFICATION of intervals_cover_unit (not its implementation) by a
// different algorithm.
[[nodiscard]] bool oracle(const CoverSpec& spec) noexcept {
    if (spec.total == 0) {
        return spec.count == 0;  // only the empty family tiles [0,0)
    }
    std::array<bool, kMaxTotal> covered{};
    for (uint32_t e = 0; e < spec.count; ++e) {
        const Iv iv = spec.ivs[e];
        if (iv.lo > iv.hi)   return false;  // malformed
        if (iv.lo == iv.hi)  return false;  // empty (note 2 rejects)
        if (iv.hi > spec.total) return false;  // out of bounds above
        for (uint64_t k = iv.lo; k < iv.hi; ++k) {
            if (covered[k]) return false;   // double-cover == overlap
            covered[k] = true;
        }
    }
    for (uint64_t k = 0; k < spec.total; ++k) {
        if (!covered[k]) return false;      // unmarked integer == gap
    }
    return true;
}

// Build a guaranteed-valid partition of [0, total) into consecutive
// non-empty pieces.  Returns the piece count (≥ 1 for total ≥ 1).
[[nodiscard]] uint32_t build_partition(
    Rng& rng, uint64_t total, std::array<Iv, kMaxIvs + 2>& out) noexcept
{
    uint32_t num_pieces = 0;
    uint64_t pos = 0;
    while (pos < total && num_pieces < kMaxIvs - 1) {
        const uint64_t remaining = total - pos;
        const uint64_t width = 1u + rng.next_below(static_cast<uint32_t>(remaining));
        out[num_pieces].lo = pos;
        out[num_pieces].hi = pos + width;
        pos += width;
        ++num_pieces;
    }
    if (pos < total) {  // final piece absorbs the remainder
        out[num_pieces].lo = pos;
        out[num_pieces].hi = total;
        ++num_pieces;
    }
    return num_pieces;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible::fuzz::prop;
    using crucible::decide::intervals_cover_unit;

    Config cfg = parse_args(argc, argv);
    if (cfg.iterations > 2'000'000) cfg.iterations = 2'000'000;  // O(total) per iter

    return run("intervals_cover_unit", cfg,
        // ── Generator: one of five partition shapes ──
        [](Rng& rng) noexcept -> CoverSpec {
            CoverSpec spec{};
            spec.mode  = static_cast<Mode>(rng.next_below(5));
            spec.total = 1u + rng.next_below(kMaxTotal);  // [1, kMaxTotal]
            switch (spec.mode) {
                case Mode::ExactPartition: {
                    spec.count = build_partition(rng, spec.total, spec.ivs);
                    // Half the time shuffle — the predicate is pairwise
                    // (order-independent), so a valid partition must
                    // stay valid under any permutation.
                    if (rng.next_below(2) != 0u) {
                        for (uint32_t i = spec.count; i > 1u; --i) {
                            const uint32_t j = rng.next_below(i);
                            std::swap(spec.ivs[i - 1u], spec.ivs[j]);
                        }
                    }
                    break;
                }
                case Mode::WithGap: {
                    spec.count = build_partition(rng, spec.total, spec.ivs);
                    const uint32_t remove_at = rng.next_below(spec.count);
                    for (uint32_t e = remove_at; e + 1u < spec.count; ++e) {
                        spec.ivs[e] = spec.ivs[e + 1u];
                    }
                    --spec.count;  // the removed piece's bytes are now a hole
                    break;
                }
                case Mode::WithOverlap: {
                    spec.count = build_partition(rng, spec.total, spec.ivs);
                    const uint32_t dup = rng.next_below(spec.count);
                    spec.ivs[spec.count] = spec.ivs[dup];  // double-cover
                    ++spec.count;
                    break;
                }
                case Mode::WithEmpty: {
                    spec.count = build_partition(rng, spec.total, spec.ivs);
                    const uint64_t k =
                        rng.next_below(static_cast<uint32_t>(spec.total) + 1u);  // [0, total]
                    spec.ivs[spec.count].lo = k;
                    spec.ivs[spec.count].hi = k;  // empty [k, k)
                    ++spec.count;
                    break;
                }
                case Mode::Random: {
                    spec.count = rng.next_below(kMaxIvs + 1u);  // [0, kMaxIvs]
                    for (uint32_t e = 0; e < spec.count; ++e) {
                        spec.ivs[e].lo =
                            rng.next_below(static_cast<uint32_t>(spec.total) + 1u);
                        spec.ivs[e].hi =
                            rng.next_below(static_cast<uint32_t>(spec.total) + 1u);
                    }
                    break;
                }
                default: std::unreachable();  // mode ∈ {0..4} by next_below(5)
            }
            return spec;
        },
        // ── Property: differential + construction-direction ──
        [](const CoverSpec& spec) noexcept -> bool {
            const std::span<const Iv> view{spec.ivs.data(), spec.count};
            const bool cut    = intervals_cover_unit(view, spec.total);
            const bool truth  = oracle(spec);

            // Universal: the code-under-test agrees with the
            // independent bitmap oracle on EVERY input.
            if (cut != truth) return false;

            // Construction-direction: pins at least one side to
            // ground truth I built, so the differential cannot pass
            // by both being co-broken the same way.
            switch (spec.mode) {
                case Mode::ExactPartition:
                    if (!cut) return false;  // a built partition MUST cover
                    break;
                case Mode::WithGap:
                case Mode::WithOverlap:
                case Mode::WithEmpty:
                    if (cut) return false;   // a built defect MUST NOT cover
                    break;
                case Mode::Random:
                    break;                   // oracle is ground truth
                default: std::unreachable();
            }
            return true;
        });
}
