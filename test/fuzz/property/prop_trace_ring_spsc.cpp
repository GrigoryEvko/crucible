// ═══════════════════════════════════════════════════════════════════
// prop_trace_ring_spsc — TraceRing SPSC append/drain invariants.
//
// Drives the SPSC ring from a single thread (producer and consumer
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
//     different slot than entries[slot] after a wrap).
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
#include "../../trace_rig.h"

#include <crucible/TraceRing.h>
#include <crucible/Types.h>

#include <cstdint>
#include <utility>

namespace {

using crucible::TraceRing;
using crucible::SchemaHash;
using crucible::ShapeHash;
using crucible::ScopeHash;
using crucible::CallsiteHash;
using crucible::MetaIndex;
using crucible::OpDepth;

// Per-iteration schedule.  Fixed-size for deterministic layout and to
// avoid heap in the input (random_input.h convention).
//
// Sizing constraint: Schedule is passed by value from the generator
// into the runner's stack frame, and the check lambda allocates an
// equally-sized mirror[] on its own frame.  Each MirrorSlot holds a
// 64-B-aligned Entry → 128 B once padded.  At MIRROR_CAPACITY=256 the
// per-iteration zero-init cost is 32 KB × 2 = 64 KB of memset, which
// at ASan's ~3 GB/s shadow-mem write rate is ~20 µs — small enough
// that 100K iters fit a few-second CI budget.  Earlier sizing of 2048
// pushed per-iter cost to ~5.6 ms (mostly ASan zero-init traffic),
// turning a 1000-iter ctest into a 6-second hang.  Full-CAPACITY
// wrap-around / two-segment memcpy is exercised by the one-shot
// stress block at the top of main(), not here.
constexpr unsigned K_PHASES = 8;
constexpr unsigned MAX_OPS_PER_PHASE = 32;
constexpr unsigned MIRROR_CAPACITY = K_PHASES * MAX_OPS_PER_PHASE;

// One queued-but-not-yet-drained append, kept on the mirror side to
// compare against the ring's output.  Stored as raw Entry bytes +
// the four parallel-array scalars so a memcmp verifies round-trip.
struct MirrorSlot {
    TraceRing::Entry entry{};
    MetaIndex meta_start{};
    ScopeHash scope_hash{};
    CallsiteHash callsite_hash{};
    OpDepth depth{};
};

// Populate `e` from RNG.  Keep scalar_types / op_flags legal so that
// subsequent reads via the accessor APIs remain well-defined — the
// ring itself doesn't dispatch on these, but avoiding garbage makes
// any future read-side invariant check meaningful.
void fill_random_entry(crucible::fuzz::prop::Rng& rng, TraceRing::Entry& e) noexcept {
    e.schema_hash = SchemaHash{rng.next64()};
    e.shape_hash = ShapeHash{rng.next64()};
    e.num_inputs = static_cast<uint16_t>(rng.next32() & 0xFFFFu);
    e.num_outputs = static_cast<uint16_t>(rng.next32() & 0xFFFFu);
    e.num_scalar_args = static_cast<uint8_t>(rng.next32() & 0xFFu);
    e.set_data_slots(crucible::DataSlots::from_bits(static_cast<uint8_t>(rng.next32() & 0x1Fu))
                         .expect("five bits name only the inline slots"));
    e.scalar_types = static_cast<uint8_t>(rng.next32() & 0xFFu);
    e.op_flags = static_cast<uint8_t>(rng.next32() & 0xFFu);
    for (auto const position : e.scalar_values.indices())
        e.scalar_values.set(position, static_cast<int64_t>(rng.next64()));
}

// Compare two Entries byte-by-byte.  Entry has no padding (8+8+2+2+1+1
// +1+1+40 = 64, all fields aligned naturally), so the compare of the
// bytes is the exact equality predicate.
[[nodiscard]] bool entry_equal(const TraceRing::Entry& a, const TraceRing::Entry& b) noexcept {
    return ::fixy::equal(::fixy::bytes(a), ::fixy::bytes(b));
}

using Mirror = ::fixy::FixedArray<MirrorSlot, MIRROR_CAPACITY>;

struct Schedule {
    // phase_op[i] = true → append; false → drain.
    ::fixy::FixedArray<bool, K_PHASES> is_append{};
    // count[i] in [0, MAX_OPS_PER_PHASE].
    ::fixy::FixedArray<uint16_t, K_PHASES> count{};
    // Precomputed random payloads for *every* potential append
    // across all phases.  Generated up-front so that generate() is
    // a pure function of the Rng state (no state held between the
    // generator and the property).
    Mirror payload{};
};

// The slot at position of a mirror or a payload.  The mirror masks its
// cursor with MIRROR_CAPACITY - 1, and a payload index stays below
// MIRROR_CAPACITY, so each position is below the capacity.
[[nodiscard]] const MirrorSlot& slot_at(const Mirror& slots CRUCIBLE_LIFETIMEBOUND, uint32_t position) noexcept {
    return slots.at(::fixy::Index<MIRROR_CAPACITY>::check(position).expect("a slot position is below the capacity"));
}
template <class... Rest>
void slot_at(const Mirror&&, Rest&&...) =
    delete("the reference names a slot of the mirror, and a temporary ends with the full expression");

}  // namespace

int crucible::fuzz::prop::run_property(::fixy::os::Args const arguments) {
    using namespace crucible;
    using namespace crucible::fuzz::prop;

    // Each iteration runs up to ~4K ring ops + O(N) mirror bookkeeping
    // — cap to keep 100K iters tractable in CI.
    const Config cfg = parse_args(arguments, 100000);

    // Hoist the 5.25 MB ring out of the per-iteration allocation path.
    // reset() restores all three cursors (head, tail, cached_tail_)
    // between iterations; payload bytes in entries[] remain but are
    // invisible because size() == 0 after reset.
    // Each door of the ring of the harness opens the side of the door for one
    // call (OpenRing of test/trace_rig.h).
    ::fixy::Box<crucible::test::OpenRing> ring =
        ::fixy::mint_box<crucible::test::OpenRing>(::foundation::effects::testing::test().alloc);

    // ── One-shot stress: fill-to-capacity + (CAP+1)-th rejects ─────
    //
    // Runs before the property loop.  The property loop uses
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
            const bool is_appended = ring->try_append(e);
            CRUCIBLE_FATAL_INVARIANT(is_appended);
        }
        auto const [discarded, discard] = ::fixy::build_fixed_array<TraceRing::Entry, PREFILL>(
            [&ring](auto run) noexcept { return ring->drain(std::move(run)); });
        CRUCIBLE_FATAL_INVARIANT(discarded == PREFILL);

        // Fill to exactly CAPACITY.
        for (uint32_t i = 0; i < CAP; ++i) {
            TraceRing::Entry e{};
            e.schema_hash = SchemaHash{uint64_t{i} + 1};  // non-zero, unique
            e.shape_hash = ShapeHash{~uint64_t{i} + 1};
            const bool is_appended =
                ring->try_append(e, MetaIndex{i}, ScopeHash{0xA0000000ULL | i}, CallsiteHash{0xB0000000ULL | i},
                                 OpDepth{static_cast<uint16_t>(i)});
            CRUCIBLE_FATAL_INVARIANT(is_appended);
        }
        CRUCIBLE_FATAL_INVARIANT(ring->size().peek() == CAP);

        // (CAP+1)-th must be rejected without mutating the ring.
        {
            TraceRing::Entry e{};
            e.schema_hash = SchemaHash{0xDEADBEEFCAFEBABEULL};
            const bool is_appended = ring->try_append(e);
            CRUCIBLE_FATAL_INVARIANT(!is_appended);  // must refuse
            CRUCIBLE_FATAL_INVARIANT(ring->size().peek() == CAP);  // must not mutate
        }

        // Drain everything; verify FIFO order + wrap-around memcpy.
        // Drain in two chunks so the first straddles the wrap.
        constexpr uint32_t CHUNK = 4096;
        auto [out_e, out_e_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<TraceRing::Entry, CHUNK>);
        auto [out_m, out_m_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<MetaIndex, CHUNK>);
        auto [out_s, out_s_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<ScopeHash, CHUNK>);
        auto [out_c, out_c_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<CallsiteHash, CHUNK>);
        auto [out_d, out_d_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<OpDepth, CHUNK>);
        uint32_t delivered = 0;
        while (delivered < CAP) {
            const uint32_t got = out_e.with_mut(out_e_key, [&](auto run_e) noexcept {
                return out_m.with_mut(out_m_key, [&](auto run_m) noexcept {
                    return out_s.with_mut(out_s_key, [&](auto run_s) noexcept {
                        return out_c.with_mut(out_c_key, [&](auto run_c) noexcept {
                            return out_d.with_mut(out_d_key, [&](auto run_d) noexcept {
                                return ring->drain(std::move(run_e), std::move(run_m), std::move(run_s),
                                                   std::move(run_c), std::move(run_d));
                            });
                        });
                    });
                });
            });
            CRUCIBLE_FATAL_INVARIANT(got != 0);  // ring has CAP entries, must deliver
            for (uint32_t k = 0; k < got; ++k) {
                const uint32_t i = delivered + k;
                const ::fixy::Index<CHUNK> slot = ::fixy::Index<CHUNK>::check(k).expect("a drain gives at most CHUNK");
                CRUCIBLE_FATAL_INVARIANT(out_e.at(slot).schema_hash == SchemaHash{uint64_t{i} + 1});
                CRUCIBLE_FATAL_INVARIANT(out_e.at(slot).shape_hash == ShapeHash{~uint64_t{i} + 1});
                CRUCIBLE_FATAL_INVARIANT(out_m.at(slot) == MetaIndex{i});
                CRUCIBLE_FATAL_INVARIANT(out_s.at(slot) == ScopeHash{0xA0000000ULL | i});
                CRUCIBLE_FATAL_INVARIANT(out_c.at(slot) == CallsiteHash{0xB0000000ULL | i});
                if (out_d.at(slot) != OpDepth{static_cast<uint16_t>(i)})
                    ::fixy::fatal("the drain changed the depth of an op");
            }
            delivered += got;
        }
        CRUCIBLE_FATAL_INVARIANT(ring->size().peek() == 0);

        // Clean slate for the property loop.
        ring->reset();
        CRUCIBLE_FATAL_INVARIANT(ring->size().peek() == 0);
        CRUCIBLE_FATAL_INVARIANT(ring->total_produced() == 0);  // post() already checked, belt+braces
    }

    // ── Randomised property loop ───────────────────────────────────

    return run(
        "TraceRing SPSC append/drain round-trip", cfg,
        [](Rng& rng) {
            Schedule s{};
            for (const ::fixy::Index<K_PHASES> p : ::fixy::IndexRange<K_PHASES>{}) {
                s.is_append.set(p, (rng.next32() & 1u) != 0u);
                s.count.set(p, static_cast<uint16_t>(rng.next_below(MAX_OPS_PER_PHASE + 1)));
            }
            for (auto const position : s.payload.indices()) {
                s.payload.update(position, [&rng](MirrorSlot& slot) noexcept {
                    fill_random_entry(rng, slot.entry);
                    // MetaIndex: occasionally use the none() sentinel,
                    // otherwise a random valid index.
                    slot.meta_start = (rng.next32() & 0xFu) == 0u ? MetaIndex{} : MetaIndex{rng.next32() & 0x00FFFFFFu};
                    slot.scope_hash = ScopeHash{rng.next64()};
                    slot.callsite_hash = CallsiteHash{rng.next64()};
                    slot.depth = OpDepth{static_cast<uint16_t>(rng.next32() & 0xFFFFu)};
                });
            }
            return s;
        },
        [&ring](const Schedule& s) -> bool {
            ring->reset();

            // Mirror queue: entries the producer successfully pushed
            // but the consumer hasn't drained yet.  Implemented as a
            // fixed-capacity circular buffer indexed by two cursors;
            // at any point the live region is [mirror_tail, mirror_head).
            Mirror mirror{};
            uint32_t mirror_head = 0;
            uint32_t mirror_tail = 0;

            // Track the index into s.payload for the next append.
            uint32_t pay_idx = 0;

            // Monotonicity witnesses.
            uint64_t last_total_produced = 0;
            uint64_t total_drained = 0;

            // Reusable drain output buffer.
            auto [out_e, out_e_key] =
                ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<TraceRing::Entry, MAX_OPS_PER_PHASE>);
            auto [out_m, out_m_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<MetaIndex, MAX_OPS_PER_PHASE>);
            auto [out_s, out_s_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<ScopeHash, MAX_OPS_PER_PHASE>);
            auto [out_c, out_c_key] =
                ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<CallsiteHash, MAX_OPS_PER_PHASE>);
            auto [out_d, out_d_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<OpDepth, MAX_OPS_PER_PHASE>);

            for (const ::fixy::Index<K_PHASES> p : ::fixy::IndexRange<K_PHASES>{}) {
                const uint16_t n = s.count.at(p);

                if (s.is_append.at(p)) {
                    for (uint16_t i = 0; i < n; ++i) {
                        if (pay_idx >= MIRROR_CAPACITY) break;  // defensive
                        const MirrorSlot& src = slot_at(s.payload, pay_idx++);

                        const uint32_t size_before = ring->size().peek();
                        const bool accepted =
                            ring->try_append(src.entry, src.meta_start, src.scope_hash, src.callsite_hash, src.depth);

                        // (B) capacity: single-threaded, cannot be full
                        //     in this harness (mirror_live ≤ 4K « CAP).
                        if (!accepted) return false;
                        if (ring->size().peek() != size_before + 1u) return false;

                        // Record on the mirror for later FIFO comparison.
                        mirror.set(::fixy::Index<MIRROR_CAPACITY>::check(mirror_head & (MIRROR_CAPACITY - 1u))
                                       .expect("a slot position is below the capacity"),
                                   src);
                        ++mirror_head;

                        // (C) producer cursor strictly advanced by 1.
                        const uint64_t tp = ring->total_produced();
                        if (tp != last_total_produced + 1u) return false;
                        last_total_produced = tp;
                    }
                } else {
                    const uint32_t want = static_cast<uint32_t>(n);
                    const uint32_t got = out_e.with_mut(out_e_key, [&](auto run_e) noexcept {
                        return out_m.with_mut(out_m_key, [&](auto run_m) noexcept {
                            return out_s.with_mut(out_s_key, [&](auto run_s) noexcept {
                                return out_c.with_mut(out_c_key, [&](auto run_c) noexcept {
                                    return out_d.with_mut(out_d_key, [&](auto run_d) noexcept {
                                        return ring->drain(::fixy::take_window(std::move(run_e), 0, want)
                                                               .expect("a phase fits the buffer"),
                                                           ::fixy::take_window(std::move(run_m), 0, want)
                                                               .expect("a phase fits the buffer"),
                                                           ::fixy::take_window(std::move(run_s), 0, want)
                                                               .expect("a phase fits the buffer"),
                                                           ::fixy::take_window(std::move(run_c), 0, want)
                                                               .expect("a phase fits the buffer"),
                                                           ::fixy::take_window(std::move(run_d), 0, want)
                                                               .expect("a phase fits the buffer"));
                                    });
                                });
                            });
                        });
                    });

                    // Consumer cannot receive more than was enqueued.
                    const uint32_t mirror_live = mirror_head - mirror_tail;
                    if (got > want) return false;
                    if (got > mirror_live) return false;
                    const uint32_t expected = ::fixy::min(want, mirror_live);
                    if (got != expected) return false;

                    // (A) round-trip FIFO equality, field-by-field.
                    for (uint32_t k = 0; k < got; ++k) {
                        const auto& want_slot = slot_at(mirror, (mirror_tail + k) & (MIRROR_CAPACITY - 1u));
                        const ::fixy::Index<MAX_OPS_PER_PHASE> slot =
                            ::fixy::Index<MAX_OPS_PER_PHASE>::check(k).expect("a drain gives at most one phase");
                        if (!entry_equal(out_e.at(slot), want_slot.entry)) return false;
                        if (out_m.at(slot) != want_slot.meta_start) return false;
                        if (out_s.at(slot) != want_slot.scope_hash) return false;
                        if (out_c.at(slot) != want_slot.callsite_hash) return false;
                        if (out_d.at(slot) != want_slot.depth) return false;
                    }
                    mirror_tail += got;
                    total_drained += got;
                }

                // (D) size() agrees with the mirror on every phase boundary.
                const uint32_t mirror_live = mirror_head - mirror_tail;
                if (ring->size().peek() != mirror_live) return false;

                // total_produced never regresses (acquire-load on a
                // producer-owned atomic we just wrote monotonically).
                if (ring->total_produced() < last_total_produced) return false;
            }

            // End-of-run: drained + still-live == total appended.
            if (total_drained + (mirror_head - mirror_tail) != mirror_head) return false;
            if (last_total_produced != mirror_head) return false;

            return true;
        });
}
