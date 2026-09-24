#pragma once

// One thread must be the sole caller of try_push(p, ...) for each producer id
// p, and one thread the sole caller of try_pop(c) for each consumer id c, both
// for the lifetime of the grid. That is what keeps every cell a genuine
// single-producer single-consumer ring. Nothing enforces it at run time.

#include <crucible/Platform.h>
#include <crucible/concurrent/_SpscRing.h>
#include <crucible/safety/_Pinned.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace crucible::concurrent {

struct RoundRobinRouting {
    template <typename T>
    [[nodiscard, gnu::const]] static std::size_t route(std::size_t /*producer_id*/, std::uint64_t seq,
                                                       std::size_t num_consumers, const T& /*item*/) noexcept {
        // size_t and uint64_t are the same type on every supported platform,
        // so the return converts exactly. An explicit cast would trip
        // -Werror=useless-cast.
        return seq % num_consumers;
    }
};

template <typename KeyFn>
struct HashKeyRouting {
    template <typename T>
    [[nodiscard, gnu::pure]] static std::size_t route(std::size_t /*producer_id*/, std::uint64_t /*seq*/,
                                                      std::size_t num_consumers, const T& item) noexcept {
        const std::uint64_t key = static_cast<std::uint64_t>(KeyFn{}(item));
        std::uint64_t h = 0xcbf29ce484222325ULL;
        for (int i = 0; i < 8; ++i) {
            h ^= (key >> (i * 8)) & 0xFFULL;
            h *= 0x100000001b3ULL;
        }
        return h % num_consumers;
    }
};

// Every producer reaches exactly one consumer, so all but one ring in each row
// stays allocated and empty. That memory buys a fixed producer-consumer pairing
// which the caller can pin to one NUMA node or one L3 group.
struct AffinityRouting {
    template <typename T>
    [[nodiscard, gnu::const]] static std::size_t route(std::size_t producer_id, std::uint64_t /*seq*/,
                                                       std::size_t num_consumers, const T& /*item*/) noexcept {
        return producer_id % num_consumers;
    }
};

template <SpscValue T, std::size_t M, std::size_t N, std::size_t Capacity, typename Routing = RoundRobinRouting>
class ShardedSpscGrid : public safety::Pinned<ShardedSpscGrid<T, M, N, Capacity, Routing>> {
public:
    using value_type = T;
    static constexpr std::size_t shard_capacity = Capacity;
    static constexpr std::size_t channel_capacity = M * N * Capacity;

    static_assert(M > 0, "ShardedSpscGrid requires at least one producer");
    static_assert(N > 0, "ShardedSpscGrid requires at least one consumer");

    ShardedSpscGrid() noexcept = default;

    [[nodiscard, gnu::hot]] bool try_push(std::size_t producer_id, const T& item) noexcept pre(producer_id < M) {
        auto& seq = producer_seqs_[producer_id].seq;
        const std::size_t consumer = Routing::route(producer_id, seq, N, item);

        // The sequence advances whether or not the push lands. Holding it back
        // on failure would route every retry to the ring that is already full.
        ++seq;

        return rings_[producer_id][consumer].try_push(item);
    }

    [[nodiscard, gnu::hot]] std::optional<T> try_pop(std::size_t consumer_id) noexcept pre(consumer_id < N) {
        auto& hint = consumer_hints_[consumer_id].next_producer;
        for (std::size_t i = 0; i < M; ++i) {
            const std::size_t p = (hint + i) % M;
            if (auto opt = rings_[p][consumer_id].try_pop()) {
                hint = (p + 1) % M;
                return opt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::size_t size_approx(std::size_t producer_id, std::size_t consumer_id) const noexcept
        pre(producer_id < M) pre(consumer_id < N) {
        return rings_[producer_id][consumer_id].size_approx();
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        std::size_t total = 0;
        for (std::size_t p = 0; p < M; ++p) {
            for (std::size_t c = 0; c < N; ++c) {
                total += rings_[p][c].size_approx();
            }
        }
        return total;
    }

    [[nodiscard]] bool empty_approx() const noexcept {
        for (std::size_t p = 0; p < M; ++p) {
            for (std::size_t c = 0; c < N; ++c) {
                if (!rings_[p][c].empty_approx()) return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr std::size_t num_producers() noexcept { return M; }
    [[nodiscard]] static constexpr std::size_t num_consumers() noexcept { return N; }
    [[nodiscard]] static constexpr std::size_t ring_capacity() noexcept { return Capacity; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return M * N * Capacity; }

private:
    // Producer-major: one producer's rings sit contiguously, so a producer
    // that routes to several consumers in succession stays in one region of
    // memory.
    std::array<std::array<SpscRing<T, Capacity>, N>, M> rings_{};

    // Each counter is written only by the thread that owns its id, so it needs
    // no atomic. The padding keeps two owning threads off one cache line.
    struct alignas(64) ProducerSeq {
        std::uint64_t seq = 0;
    };
    std::array<ProducerSeq, M> producer_seqs_{};

    struct alignas(64) ConsumerHint {
        std::size_t next_producer = 0;
    };
    std::array<ConsumerHint, N> consumer_hints_{};
};

}  // namespace crucible::concurrent
