// ═══════════════════════════════════════════════════════════════════
// prop_trace_ring_spsc — TraceRing SPSC append/drain invariants.
//
// Drives the SPSC ring from a SINGLE thread (producer and consumer
// serialised).  Real-world cross-thread atomicity is covered by the
// TSan suite + test_trace_ring.cpp's jthread producer/consumer — this
// harness hunts for algorithmic/structural defects in the ring
// itself: wrap-around, cursor arithmetic, per-slot parallel-array
// desync, reset state corruption.
//
// Per iteration:
//   1. A random schedule of K phases.
//   2. Each phase is either APPEND(n) or DRAIN(n).
//   3. Each appended Entry carries random schema/shape hashes,
//      counts, flags, inline scalars, plus randomly-chosen
//      MetaIndex / ScopeHash / CallsiteHash.
//   4. After every op, we verify:
//        (A) round-trip: what was drained is byte-equal, in FIFO
//            order, to what the producer succeeded in enqueuing.
//        (B) capacity: try_append returns false iff (size() == CAP).
//        (C) cursor monotonicity: total_produced() and the consumer's
//            drained-count are non-decreasing across the whole run.
//        (D) size() == head - tail relation agrees with the internal
//            mirror at every checkpoint.
//
// After the property loop, a one-shot stress block fills the ring to
// exactly CAPACITY, proves the (CAP+1)-th append returns false, then
// drains + verifies the full-capacity round-trip (two-segment memcpy
// path is exercised by wrapping the tail partway first).
//
// Catches:
//   - Off-by-one in `h - cached_tail_` full-check (the silent-drop
//     path that would lose the (CAP+1)-th entry without telling
//     the caller, instead of returning false).
//   - Producer forgetting to update head on success, or consumer
//     forgetting to update tail (one-directional cursor desync).
//   - Parallel-array slot slip (meta_starts[slot] reading from a
//     DIFFERENT slot than entries[slot] after a wrap).
//   - `reset()` failing to zero head/tail/cached_tail_ — next
//     append would inherit stale head and confuse the invariant.
//   - Byte-level Entry mismatch (a misaligned memcpy, partial
//     struct copy, or stale padding survived across slots).
//
// Scale: K=16 phases × up to 256 ops each = ~4K ring ops/iter.
// 100K iterations ≈ 400M ring ops exercised end-to-end.
// ═══════════════════════════════════════════════════════════════════

#include "property_runner.h"
#include "random_input.h"

#include <crucible/TraceRing.h>
#include <crucible/Types.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

namespace {

using crucible::TraceRing;
using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::MetaIndex;

// Per-iteration schedule.  Fixed-size for deterministic layout and to
// avoid heap in the input (random_input.h convention).
//
// Sizing constraint: Schedule is passed by value from the generator
// into the runner's stack frame, AND the check lambda allocates an
// equally-sized mirror[] on its own frame.  Each MirrorSlot holds a
// 64-B-aligned Entry → 128 B once padded.  At MIRROR_CAPACITY=256 the
// per-iteration zero-init cost is 32 KB × 2 = 64 KB of memset, which
// at ASan's ~3 GB/s shadow-mem write rate is ~20 µs — small enough
// that 100K iters fit a few-second CI budget.  Earlier sizing of 2048
// pushed per-iter cost to ~5.6 ms (mostly ASan zero-init traffic),
// turning a 1000-iter ctest into a 6-second hang.  Full-CAPACITY
// wrap-around / two-segment memcpy is exercised by the one-shot
// stress block at the top of main(), not here.
constexpr unsigned K_PHASES          = 8;
constexpr unsigned MAX_OPS_PER_PHASE = 32;
constexpr unsigned MIRROR_CAPACITY   = K_PHASES * MAX_OPS_PER_PHASE;

// One queued-but-not-yet-drained append, kept on the mirror side to
// compare against the ring's output.  Stored as raw Entry bytes +
// the three parallel-array scalars so a memcmp verifies round-trip.
struct MirrorSlot {
    TraceRing::Entry entry{};
    MetaIndex        meta_start{};
    ScopeHash        scope_hash{};
    CallsiteHash     callsite_hash{};
};

// Populate `e` from RNG.  Keep scalar_types / op_flags legal so that
// subsequent reads via the accessor APIs remain well-defined — the
// ring itself doesn't dispatch on these, but avoiding garbage makes
// any future read-side invariant check meaningful.
void fill_random_entry(crucible::fuzz::prop::Rng& rng,
                       TraceRing::Entry& e) noexcept {
    e.schema_hash     = SchemaHash{rng.next64()};
    e.shape_hash      = ShapeHash{rng.next64()};
    e.num_inputs      = static_cast<uint16_t>(rng.next32() & 0xFFFFu);
    e.num_outputs     = static_cast<uint16_t>(rng.next32() & 0xFFFFu);
    e.num_scalar_args = static_cast<uint16_t>(rng.next32() & 0xFFFFu);
    e.scalar_types    = static_cast<uint8_t >(rng.next32() & 0xFFu);
    e.op_flags        = static_cast<uint8_t >(rng.next32() & 0xFFu);
    for (auto& v : e.scalar_values) v = static_cast<int64_t>(rng.next64());
}

// Compare two Entries byte-by-byte.  Entry has no padding (8+8+2+2+2
// +1+1+40 = 64, all fields aligned naturally), so memcmp is the exact
// equality predicate.
[[nodiscard]] bool entry_equal(const TraceRing::Entry& a,
                               const TraceRing::Entry& b) noexcept {
    return std::memcmp(&a, &b, sizeof(TraceRing::Entry)) == 0;
}

struct Schedule {
    // phase_op[i] = true → append; false → drain.
    std::array<bool, K_PHASES>     is_append{};
    // count[i] in [0, MAX_OPS_PER_PHASE].
    std::array<uint16_t, K_PHASES> count{};
    // Precomputed random payloads for *every* potential append
    // across all phases.  Generated up-front so that generate() is
    // a pure function of the Rng state (no state held between the
    // generator and the property).
    std::array<MirrorSlot, MIRROR_CAPACITY> payload{};
};

}  // namespace

int main(int argc, char** argv) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;

    Config cfg = parse_args(argc, argv);
    // Each iteration runs up to ~4K ring ops + O(N) mirror bookkeeping
    // — cap to keep 100K iters tractable in CI.
    if (cfg.iterations > 100'000) cfg.iterations = 100'000;

    // Hoist the 5.25 MB ring out of the per-iteration allocation path.
    // reset() restores all three cursors (head, tail, cached_tail_)
    // between iterations; payload bytes in entries[] remain but are
    // invisible because size() == 0 after reset.
    auto ring = std::make_unique<TraceRing>();

    // ── One-shot stress: fill-to-capacity + (CAP+1)-th rejects ─────
    //
    // Runs BEFORE the property loop.  The property loop uses
    // MIRROR_CAPACITY = 4K slots, which can't reach CAP = 64K, so
    // the capacity-respect property for the full ring has to be
    // exercised separately.  We also seed a partial drain first so
    // the two-segment wrap path is hit when filling to CAP.
    {
        constexpr uint32_t CAP = TraceRing::CAPACITY;
        // Wrap the start: push N, drain N — head==tail==N, so the
        // next fill wraps when slot index crosses CAP.
        constexpr uint32_t PREFILL = 123;
        for (uint32_t i = 0; i < PREFILL; ++i) {
            TraceRing::Entry e{};
            e.schema_hash = SchemaHash{0xBEEF0000ULL | i};
            if (!ring->try_append(e)) std::abort();
        }
        std::array<TraceRing::Entry, PREFILL> discard{};
        if (ring->drain(discard.data(), PREFILL) != PREFILL) std::abort();

        // Fill to exactly CAPACITY.
        for (uint32_t i = 0; i < CAP; ++i) {
            TraceRing::Entry e{};
            e.schema_hash = SchemaHash{uint64_t{i} + 1};  // non-zero, unique
            e.shape_hash  = ShapeHash{~uint64_t{i} + 1};
            if (!ring->try_append(e,
                                  MetaIndex{i},
                                  ScopeHash{0xA000'0000ULL | i},
                                  CallsiteHash{0xB000'0000ULL | i})) std::abort();
        }
        if (ring->size() != CAP) std::abort();

        // (CAP+1)-th must be rejected without mutating the ring.
        {
            TraceRing::Entry e{};
            e.schema_hash = SchemaHash{0xDEADBEEFCAFEBABEULL};
            if (ring->try_append(e)) std::abort();      // must refuse
            if (ring->size() != CAP)  std::abort();      // must not mutate
        }

        // Drain everything; verify FIFO order + wrap-around memcpy.
        // Drain in two chunks so the first straddles the wrap.
        constexpr uint32_t CHUNK = 4096;
        std::array<TraceRing::Entry, CHUNK> out_e{};
        std::array<MetaIndex,        CHUNK> out_m{};
        std::array<ScopeHash,        CHUNK> out_s{};
        std::array<CallsiteHash,     CHUNK> out_c{};
        uint32_t delivered = 0;
        while (delivered < CAP) {
            const uint32_t got = ring->drain(out_e.data(), CHUNK,
                                             out_m.data(),
                                             out_s.data(),
                                             out_c.data());
            if (got == 0) std::abort();  // ring has CAP entries, must deliver
            for (uint32_t k = 0; k < got; ++k) {
                const uint32_t i = delivered + k;
                if (out_e[k].schema_hash != SchemaHash{uint64_t{i} + 1}) std::abort();
                if (out_e[k].shape_hash  != ShapeHash{~uint64_t{i} + 1}) std::abort();
                if (out_m[k] != MetaIndex{i})                            std::abort();
                if (out_s[k] != ScopeHash{0xA000'0000ULL | i})           std::abort();
                if (out_c[k] != CallsiteHash{0xB000'0000ULL | i})        std::abort();
            }
            delivered += got;
        }
        if (ring->size() != 0) std::abort();

        // Clean slate for the property loop.
        ring->reset();
        if (ring->size()           != 0) std::abort();
        if (ring->total_produced() != 0) std::abort();  // post() already checked, belt+braces
    }

    // ── Randomised property loop ───────────────────────────────────

    return run("TraceRing SPSC append/drain round-trip", cfg,
        [](Rng& rng) {
            Schedule s{};
            for (unsigned p = 0; p < K_PHASES; ++p) {
                s.is_append[p] = (rng.next32() & 1u) != 0u;
                s.count[p]     = static_cast<uint16_t>(
                    rng.next_below(MAX_OPS_PER_PHASE + 1));
            }
            for (auto& slot : s.payload) {
                fill_random_entry(rng, slot.entry);
                // MetaIndex: occasionally use the none() sentinel,
                // otherwise a random valid index.
                slot.meta_start = (rng.next32() & 0xFu) == 0u
                    ? MetaIndex{}
                    : MetaIndex{rng.next32() & 0x00FF'FFFFu};
                slot.scope_hash    = ScopeHash{rng.next64()};
                slot.callsite_hash = CallsiteHash{rng.next64()};
            }
            return s;
        },
        [&ring](const Schedule& s) -> bool {
            ring->reset();

            // Mirror queue: entries the producer successfully pushed
            // but the consumer hasn't drained yet.  Implemented as a
            // fixed-capacity circular buffer indexed by two cursors;
            // at any point the live region is [mirror_tail, mirror_head).
            std::array<MirrorSlot, MIRROR_CAPACITY> mirror{};
            uint32_t mirror_head = 0;
            uint32_t mirror_tail = 0;

            // Track the index into s.payload for the NEXT append.
            uint32_t pay_idx = 0;

            // Monotonicity witnesses.
            uint64_t last_total_produced = 0;
            uint64_t total_drained       = 0;

            // Reusable drain output buffer.
            std::array<TraceRing::Entry, MAX_OPS_PER_PHASE> out_e{};
            std::array<MetaIndex,        MAX_OPS_PER_PHASE> out_m{};
            std::array<ScopeHash,        MAX_OPS_PER_PHASE> out_s{};
            std::array<CallsiteHash,     MAX_OPS_PER_PHASE> out_c{};

            for (unsigned p = 0; p < K_PHASES; ++p) {
                const uint16_t n = s.count[p];

                if (s.is_append[p]) {
                    for (uint16_t i = 0; i < n; ++i) {
                        if (pay_idx >= MIRROR_CAPACITY) break;  // defensive
                        const MirrorSlot& src = s.payload[pay_idx++];

                        const uint32_t size_before = ring->size();
                        const bool     accepted    = ring->try_append(
                            src.entry, src.meta_start,
                            src.scope_hash, src.callsite_hash);

                        // (B) capacity: single-threaded, cannot be full
                        //     in this harness (mirror_live ≤ 4K « CAP).
                        if (!accepted) return false;
                        if (ring->size() != size_before + 1u) return false;

                        // Record on the mirror for later FIFO comparison.
                        mirror[mirror_head & (MIRROR_CAPACITY - 1u)] = src;
                        ++mirror_head;

                        // (C) producer cursor strictly advanced by 1.
                        const uint64_t tp = ring->total_produced();
                        if (tp != last_total_produced + 1u) return false;
                        last_total_produced = tp;
                    }
                } else {
                    const uint32_t want = static_cast<uint32_t>(n);
                    const uint32_t got  = ring->drain(
                        out_e.data(), want,
                        out_m.data(), out_s.data(), out_c.data());

                    // Consumer cannot receive more than was enqueued.
                    const uint32_t mirror_live = mirror_head - mirror_tail;
                    if (got > want)        return false;
                    if (got > mirror_live) return false;
                    const uint32_t expected = std::min(want, mirror_live);
                    if (got != expected)   return false;

                    // (A) round-trip FIFO equality, field-by-field.
                    for (uint32_t k = 0; k < got; ++k) {
                        const auto& want_slot =
                            mirror[(mirror_tail + k) & (MIRROR_CAPACITY - 1u)];
                        if (!entry_equal(out_e[k], want_slot.entry))    return false;
                        if (out_m[k] != want_slot.meta_start)           return false;
                        if (out_s[k] != want_slot.scope_hash)           return false;
                        if (out_c[k] != want_slot.callsite_hash)        return false;
                    }
                    mirror_tail    += got;
                    total_drained  += got;
                }

                // (D) size() agrees with the mirror on every phase boundary.
                const uint32_t mirror_live = mirror_head - mirror_tail;
                if (ring->size() != mirror_live) return false;

                // total_produced never regresses (acquire-load on a
                // producer-owned atomic we just wrote monotonically).
                if (ring->total_produced() < last_total_produced) return false;
            }

            // End-of-run: drained + still-live == total appended.
            if (total_drained + (mirror_head - mirror_tail) != mirror_head)
                return false;
            if (last_total_produced != mirror_head) return false;

            return true;
        });
}
