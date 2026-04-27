#pragma once

// ═══════════════════════════════════════════════════════════════════
// ShardedSpscGrid<T, M, N, Capacity, Routing> — M producers × N
// consumers via M×N SPSC rings, formalizing the
// "stay SPSC and shard" pattern.
//
// Instead of building one MPMC queue (CAS on every op for both
// producer and consumer ends), build M×N independent SpscRings and
// route at the producer side.  Each ring cell `rings[p][c]` is
// touched by exactly one producer thread `p` and exactly one
// consumer thread `c` — no inter-producer or inter-consumer
// contention.  Per-op cost stays at the SPSC shape: one acquire/
// release atomic on isolated cache lines, no CAS.
//
// ─── The "decompose to SPSC" principle ──────────────────────────────
//
// MPMC queues pay CAS on every op because multiple producers race
// for the head and multiple consumers race for the tail.  But many
// real workloads have STRUCTURE: producers can be identified by
// thread ID; consumers can be identified by topic, hash, or
// priority.  When that structure is available, the M×N grid maps
// it directly to SPSC primitives — no CAS, predictable cache
// traffic, lock-free without the contention tax.
//
// Examples:
//   * Fan-in dispatch from V Vessel threads to one Vigil consumer:
//     ShardedSpscGrid<DispatchEvent, V, 1, 4096, RoundRobinRouting>
//     (V producers → 1 consumer; degenerate single-column grid)
//
//   * Topic-keyed event routing (per-key ordering):
//     ShardedSpscGrid<Event, M, N, 1024, HashKeyRouting<EventTopic>>
//     (events with the same topic always go to the same consumer)
//
//   * Compile pool alternative to ChaseLevDeque (if work-stealing
//     overhead measures higher than direct sharding):
//     ShardedSpscGrid<CompileTask, 1, 8, 256, HashKeyRouting<JobHash>>
//
// ─── Routing policies ───────────────────────────────────────────────
//
//   RoundRobinRouting: consumer = seq % N
//     - Even load distribution
//     - No per-key ordering (consecutive items from the same
//       producer go to different consumers)
//
//   HashKeyRouting<KeyFn>: consumer = hash(KeyFn(item)) % N
//     - Per-key ordering preserved (same key → same consumer)
//     - Deterministic — same item always routes to same consumer
//
//   Custom: any type with a static `route(producer_id, seq,
//     num_consumers, item)` method matching the signature.
//
// ─── Constraints ────────────────────────────────────────────────────
//
//   * T must satisfy SpscValue (trivially-copyable + trivially-
//     destructible)
//   * M, N, Capacity all compile-time fixed power-of-two etc.
//   * Producer p MUST be the sole caller of send(p, ...) for the
//     grid's lifetime.  Consumer c MUST be the sole caller of
//     try_recv(c).  Caller pins these — no runtime check.
//   * Memory cost: M × N × sizeof(SpscRing<T, Capacity>).  For
//     M=4, N=4, T=ptr (8 B), Cap=1024: 16 rings × ~8.4 KB ≈ 134 KB.
//     Acceptable at hot paths; prohibitive at N=32+.  Document the
//     budget in the task or use ChaseLevDeque if memory is tight.
// ═══════════════════════════════════════════════════════════════════

#include <crucible/Platform.h>
#include <crucible/concurrent/SpscRing.h>
#include <crucible/safety/Pinned.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace crucible::concurrent {

// ── Routing policies ─────────────────────────────────────────────

// RoundRobinRouting — consumer = seq % N.
// Producer threads have their own seq counter, so this gives even
// distribution without inter-producer coordination.
struct RoundRobinRouting {
    template <typename T>
    [[nodiscard, gnu::const]] static std::size_t route(
        std::size_t /*producer_id*/, std::uint64_t seq,
        std::size_t num_consumers, const T& /*item*/) noexcept {
        // size_t == uint64_t on supported platforms; implicit
        // conversion exact, explicit cast triggers -Werror=useless-cast.
        return seq % num_consumers;
    }
};

// HashKeyRouting<KeyFn> — consumer = fnv1a(KeyFn(item)) % N.
//
// KeyFn must be a default-constructible callable returning a
// uint64_t (or convertible).  Same key → same consumer always:
// preserves per-key ordering across the grid.
template <typename KeyFn>
struct HashKeyRouting {
    template <typename T>
    [[nodiscard, gnu::pure]] static std::size_t route(
        std::size_t /*producer_id*/, std::uint64_t /*seq*/,
        std::size_t num_consumers, const T& item) noexcept {
        const std::uint64_t key = static_cast<std::uint64_t>(KeyFn{}(item));
        // FNV-1a over the 8 bytes of `key` — small constant cost,
        // good distribution.
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (int i = 0; i < 8; ++i) {
            h ^= (key >> (i * 8)) & 0xFFULL;
            h *= 0x100000001b3ULL;
        }
        return h % num_consumers;
    }
};

// ── ShardedSpscGrid<T, M, N, Capacity, Routing> ──────────────────

template <SpscValue T,
          std::size_t M,
          std::size_t N,
          std::size_t Capacity,
          typename Routing = RoundRobinRouting>
class ShardedSpscGrid : public safety::Pinned<
    ShardedSpscGrid<T, M, N, Capacity, Routing>> {
public:
    static_assert(M > 0, "ShardedSpscGrid requires at least one producer");
    static_assert(N > 0, "ShardedSpscGrid requires at least one consumer");

    ShardedSpscGrid() noexcept = default;

    // ── send (sole producer for producer_id) ──────────────────────
    //
    // Producer `producer_id` sends `item` into the grid.  Routing
    // policy selects which consumer ring receives it.  Returns
    // false if the selected ring is full — caller may retry, route
    // around, or drop.
    //
    // Caller MUST guarantee `producer_id < M` AND that the calling
    // thread is the sole producer for that id.
    [[nodiscard, gnu::hot]] bool send(std::size_t producer_id,
                                       const T& item) noexcept
        pre (producer_id < M)
    {
        // Per-producer sequence counter for routing decisions.
        // Single-producer access — no atomic needed (caller owns
        // this producer slot exclusively).
        auto& seq = producer_seqs_[producer_id].seq;
        const std::size_t consumer =
            Routing::route(producer_id, seq, N, item);

        // Advance seq regardless of push success — keeps round-robin
        // fair under back-pressure (a full ring doesn't pin all
        // future routes to the same consumer).
        ++seq;

        return rings_[producer_id][consumer].try_push(item);
    }

    // ── try_recv (sole consumer for consumer_id) ──────────────────
    //
    // Consumer `consumer_id` reads from any of the M rings in its
    // column.  Round-robin across producers via a per-consumer hint
    // for fairness — no producer's stream gets starved by a chatty
    // peer.
    //
    // Caller MUST guarantee `consumer_id < N` AND that the calling
    // thread is the sole consumer for that id.
    [[nodiscard, gnu::hot]] std::optional<T> try_recv(
        std::size_t consumer_id) noexcept
        pre (consumer_id < N)
    {
        // Per-consumer fairness cursor — single-consumer access,
        // no atomic needed.
        auto& hint = consumer_hints_[consumer_id].next_producer;
        for (std::size_t i = 0; i < M; ++i) {
            const std::size_t p = (hint + i) % M;
            if (auto opt = rings_[p][consumer_id].try_pop()) {
                // Advance hint past this producer for next call.
                hint = (p + 1) % M;
                return opt;
            }
        }
        return std::nullopt;
    }

    // ── Diagnostics (snapshot, NOT exact) ─────────────────────────

    [[nodiscard]] std::size_t size_approx(std::size_t producer_id,
                                           std::size_t consumer_id) const noexcept
        pre (producer_id < M)
        pre (consumer_id < N)
    {
        return rings_[producer_id][consumer_id].size_approx();
    }

    [[nodiscard]] static constexpr std::size_t num_producers() noexcept { return M; }
    [[nodiscard]] static constexpr std::size_t num_consumers() noexcept { return N; }
    [[nodiscard]] static constexpr std::size_t ring_capacity() noexcept { return Capacity; }

private:
    // M×N grid of SPSC rings.  Each ring[p][c] is independently
    // SPSC: producer p is the sole writer, consumer c is the sole
    // reader.
    //
    // Outer array indexed by producer (M), inner by consumer (N).
    // Layout means producer p's rings are contiguous in memory —
    // good cache locality when one producer routes to multiple
    // consumers in succession.
    std::array<std::array<SpscRing<T, Capacity>, N>, M> rings_{};

    // Per-producer sequence counter.  Each on its own cache line
    // to avoid false sharing between producer threads.
    struct alignas(64) ProducerSeq {
        std::uint64_t seq = 0;
    };
    std::array<ProducerSeq, M> producer_seqs_{};

    // Per-consumer round-robin cursor.  Each on its own cache line
    // to avoid false sharing between consumer threads.
    struct alignas(64) ConsumerHint {
        std::size_t next_producer = 0;
    };
    std::array<ConsumerHint, N> consumer_hints_{};
};

}  // namespace crucible::concurrent
