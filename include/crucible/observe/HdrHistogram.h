#pragma once

// crucible::observe::HdrHistogram
//
// Fixed-shape high dynamic range histogram for latency distributions.
// The recording path is one bucket-index calculation plus one relaxed
// atomic increment; all storage is static and contiguous.

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/safety/Refined.h>

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace crucible::observe {

namespace detail {

[[nodiscard]] consteval std::uint64_t pow10_u64(std::uint8_t n) noexcept {
    std::uint64_t value = 1;
    for (std::uint8_t i = 0; i < n; ++i) {
        value *= 10;
    }
    return value;
}

[[nodiscard]] consteval std::uint64_t next_power_of_two(std::uint64_t x) noexcept {
    if (x <= 1) {
        return 1;
    }
    return std::uint64_t{1} << std::bit_width(x - 1);
}

template <std::uint8_t Significant, std::uint64_t MaxValue>
struct HdrLayout {
    static_assert(Significant >= 1 && Significant <= 5,
                  "HdrHistogram Significant must be in [1, 5]");
    static_assert(MaxValue > 0, "HdrHistogram MaxValue must be positive");

    static constexpr std::uint64_t sub_bucket_count =
        next_power_of_two(pow10_u64(Significant) * 2);
    static constexpr std::uint64_t sub_bucket_half_count = sub_bucket_count / 2;
    static constexpr unsigned sub_bucket_half_count_magnitude =
        std::countr_zero(sub_bucket_half_count);
    static constexpr std::uint64_t sub_bucket_mask = sub_bucket_count - 1;

    [[nodiscard]] static constexpr std::uint32_t bucket_index(std::uint64_t value) noexcept {
        const std::uint64_t normalized = value | sub_bucket_mask;
        const auto bit_count = static_cast<unsigned>(std::bit_width(normalized));
        return bit_count > sub_bucket_half_count_magnitude + 1
            ? bit_count - sub_bucket_half_count_magnitude - 1
            : 0;
    }

    static constexpr std::uint32_t bucket_count = bucket_index(MaxValue) + 1;
    static constexpr std::size_t counts_len =
        static_cast<std::size_t>(bucket_count + 1) *
        static_cast<std::size_t>(sub_bucket_half_count);

    [[nodiscard]] static constexpr std::size_t counts_index(std::uint64_t value) noexcept {
        const std::uint32_t bucket = bucket_index(value);
        if (bucket == 0) {
            return static_cast<std::size_t>(value);
        }
        const std::uint64_t sub_bucket = value >> bucket;
        const std::uint64_t offset = sub_bucket - sub_bucket_half_count;
        return (static_cast<std::size_t>(bucket + 1) *
                static_cast<std::size_t>(sub_bucket_half_count)) +
               static_cast<std::size_t>(offset);
    }

    [[nodiscard]] static constexpr std::uint64_t value_from_index(std::size_t index) noexcept {
        const std::uint64_t half = sub_bucket_half_count;
        std::uint64_t bucket = (index >> sub_bucket_half_count_magnitude);
        std::uint64_t sub_bucket = (index & (half - 1)) + half;

        if (bucket == 0) {
            sub_bucket -= half;
        } else {
            --bucket;
        }

        return sub_bucket << bucket;
    }
};

}  // namespace detail

template <
    std::uint8_t Significant = 3,
    std::uint64_t MaxValue = 3'600'000'000'000ull>
class HdrHistogram {
public:
    using layout_type = detail::HdrLayout<Significant, MaxValue>;
    // fixy-A5-008: the lower bound is 0, not 1.  Production transports
    // emit zero-valued samples on cold paths (RTT before the first
    // round-trip, bandwidth before the first byte, queue depth on
    // an idle fd) and the old `in_range<1, MaxValue>` precondition
    // fired `std::terminate` inside Refined<>'s contract — aborting
    // the Keeper daemon on first-sample-after-socket-open.  The
    // bucket math handles zero correctly: counts_index(0) returns 0,
    // value_from_index(0) returns 0, and percentile() already gates
    // total == 0.  Widening the refinement is the structural fix;
    // narrowing it later would require either a separate "zero"
    // sentinel bucket OR per-call gating in every caller — both
    // strictly worse than letting zero be a first-class sample value.
    using value_type =
        safety::Refined<safety::in_range<std::uint64_t{0}, MaxValue>, std::uint64_t>;

    struct EncodedBucket {
        std::uint32_t index;
        std::uint64_t lowest_value;
        std::uint64_t count;
    };

    struct LogEncodedBucket {
        std::uint32_t index;
        std::uint64_t count;
    };

    struct SerializeResult {
        std::size_t written;
        std::size_t required;

        [[nodiscard]] constexpr bool complete() const noexcept {
            return written == required;
        }
    };

    static constexpr std::uint8_t significant_digits = Significant;
    static constexpr std::uint64_t max_trackable_value = MaxValue;
    static constexpr std::size_t bucket_slots = layout_type::counts_len;
    static_assert(layout_type::counts_index(std::uint64_t{0}) < bucket_slots,
                  "fixy-A5-008: bucket 0 must be a valid slot — zero samples "
                  "are first-class inputs (cold-path RTT/BW/queue-depth)");
    static_assert(layout_type::counts_index(std::uint64_t{1}) < bucket_slots);
    static_assert(layout_type::counts_index(MaxValue) < bucket_slots);

    HdrHistogram() = default;
    HdrHistogram(const HdrHistogram&) = delete;
    HdrHistogram& operator=(const HdrHistogram&) = delete;

    [[nodiscard]] static constexpr value_type checked_value(std::uint64_t value) noexcept {
        return value_type{value};
    }

    CRUCIBLE_HOT void record(value_type value) noexcept {
        const std::size_t index = layout_type::counts_index(value.value());
        counts_[index].fetch_add(1, std::memory_order_relaxed);
        // fixy-A5-007: acq_rel (was release-only) is load-bearing for
        // N-producer races on weakly-ordered targets (ARM, Apple
        // Silicon, Graviton).  A release-only RMW does NOT synchronize
        // with a SECOND producer's release-only RMW: producer-Y reads
        // producer-X's incremented value but the read part of Y's RMW
        // has no acquire semantics, so X's prior counts_ stores are
        // not happens-before Y's release.  A reader whose acquire-
        // load reads Y's release sees Y's counts_ updates but NOT X's
        // → sum(counts_) < total_count, observable as a torn percentile
        // (loop falls off the end and returns MaxValue).  acq_rel
        // builds the transitive chain: X's counts_ → X's release → Y's
        // acquire-RMW → Y's release → Reader's acquire.  On x86 the
        // generated code is unchanged (LOCK XADD is already full-
        // barrier); ARM gains the missing DMB-ISH fence.
        total_count_.fetch_add(1, std::memory_order_acq_rel);
    }

    [[nodiscard]] std::uint64_t total_count() const noexcept {
        return total_count_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t percentile(double pct) const noexcept {
        const std::uint64_t total = total_count();
        if (total == 0 || !(pct > 0.0)) {
            return 0;
        }
        if (pct > 100.0) {
            pct = 100.0;
        }

        const long double rank_f =
            (static_cast<long double>(pct) / 100.0L) * static_cast<long double>(total);
        std::uint64_t rank = static_cast<std::uint64_t>(std::ceil(rank_f));
        if (rank == 0) {
            rank = 1;
        }

        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            seen += counts_[i].load(std::memory_order_relaxed);
            if (seen >= rank) {
                return layout_type::value_from_index(i);
            }
        }
        return MaxValue;
    }

    [[nodiscard]] std::uint64_t mean() const noexcept {
        const std::uint64_t total = total_count();
        if (total == 0) {
            return 0;
        }

        long double sum = 0.0L;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = counts_[i].load(std::memory_order_relaxed);
            if (count != 0) {
                sum += static_cast<long double>(layout_type::value_from_index(i)) *
                       static_cast<long double>(count);
            }
        }
        return static_cast<std::uint64_t>(
            (sum / static_cast<long double>(total)) + 0.5L);
    }

    [[nodiscard]] std::uint64_t std_dev() const noexcept {
        const std::uint64_t total = total_count();
        if (total == 0) {
            return 0;
        }

        const long double avg = static_cast<long double>(mean());
        long double variance = 0.0L;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = counts_[i].load(std::memory_order_relaxed);
            if (count != 0) {
                const long double delta =
                    static_cast<long double>(layout_type::value_from_index(i)) - avg;
                variance += delta * delta * static_cast<long double>(count);
            }
        }
        return static_cast<std::uint64_t>(
            std::sqrt(static_cast<double>(variance / static_cast<long double>(total))) +
            0.5);
    }

    void merge_from(const HdrHistogram& other) noexcept {
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = other.counts_[i].load(std::memory_order_relaxed);
            if (count != 0) {
                counts_[i].fetch_add(count, std::memory_order_relaxed);
            }
        }
        // fixy-A5-007: acq_rel (was release) for the same N-producer
        // race reasoning as `record` — concurrent merge_from calls into
        // the same `this` histogram form an RMW chain on total_count_
        // that needs acquire-side to publish prior bucket writes.
        total_count_.fetch_add(other.total_count(), std::memory_order_acq_rel);
    }

    void add_from(const HdrHistogram& other) noexcept {
        merge_from(other);
    }

    // fixy-A5-007 supersedes fixy-A5-019: publish discipline matches
    // merge_from and record — acq_rel on total_count_ updates.  The
    // prior A5-019 rationale that "no value read after the CAS depends
    // on other threads' writes" missed the N-producer publishing case:
    // two concurrent subtract_from calls (or one subtract + one merge)
    // form an RMW chain on total_count_ that needs acquire-side
    // semantics to publish prior bucket writes across the chain.
    // saturating_sub's CAS-retry loop already uses an implicit acquire
    // on its read side, but the SUCCESS ordering decides the publish
    // semantics — acq_rel is the load-bearing case.
    void subtract_from(const HdrHistogram& other) noexcept {
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = other.counts_[i].load(std::memory_order_relaxed);
            if (count != 0) {
                saturating_sub(counts_[i], count);
            }
        }
        saturating_sub(total_count_, other.total_count(), std::memory_order_acq_rel);
    }

    void reset() noexcept {
        for (auto& count : counts_) {
            count.store(0, std::memory_order_relaxed);
        }
        total_count_.store(0, std::memory_order_relaxed);
    }

    template <typename Fn>
    std::size_t for_each_nonzero(Fn&& fn) const {
        std::size_t emitted = 0;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = counts_[i].load(std::memory_order_relaxed);
            if (count != 0) {
                fn(EncodedBucket{
                    .index = static_cast<std::uint32_t>(i),
                    .lowest_value = layout_type::value_from_index(i),
                    .count = count,
                });
                ++emitted;
            }
        }
        return emitted;
    }

    SerializeResult serialize_log_into(std::span<LogEncodedBucket> out) const noexcept {
        SerializeResult result{.written = 0, .required = 0};
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = counts_[i].load(std::memory_order_relaxed);
            if (count == 0) {
                continue;
            }
            if (result.written < out.size()) {
                out[result.written] = LogEncodedBucket{
                    .index = static_cast<std::uint32_t>(i),
                    .count = count,
                };
                ++result.written;
            }
            ++result.required;
        }
        return result;
    }

private:
    static void saturating_sub(std::atomic<std::uint64_t>& dst,
                               std::uint64_t amount,
                               std::memory_order success =
                                   std::memory_order_relaxed) noexcept {
        std::uint64_t observed = dst.load(std::memory_order_relaxed);
        while (true) {
            const std::uint64_t desired = observed > amount ? observed - amount : 0;
            if (dst.compare_exchange_weak(
                    observed, desired,
                    success,
                    std::memory_order_relaxed)) {
                return;
            }
        }
    }

    alignas(64) std::array<std::atomic<std::uint64_t>, bucket_slots> counts_{};
    alignas(64) std::atomic<std::uint64_t> total_count_{0};
};

// fixy-A5-029: cross-thread atomics on the HDR record / percentile path must
// be lock-free on every supported target.  libstdc++ silently substitutes
// mutex-backed atomic ops on ISAs lacking the required intrinsic — a hidden
// mutex inside every record() / total_count() would invert the observe
// histogram's microsecond budget.  Refuse to build instead of regressing.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this target — "
              "fixy-A5-029");

template <
    std::uint8_t Significant = 3,
    std::uint64_t MaxValue = 3'600'000'000'000ull,
    std::size_t ShardCount = 4>
class ConcurrentHdrHistogram {
    static_assert(ShardCount > 0, "ConcurrentHdrHistogram needs at least one shard");

public:
    using histogram_type = HdrHistogram<Significant, MaxValue>;
    using value_type = typename histogram_type::value_type;

    ConcurrentHdrHistogram() = default;
    ConcurrentHdrHistogram(const ConcurrentHdrHistogram&) = delete;
    ConcurrentHdrHistogram& operator=(const ConcurrentHdrHistogram&) = delete;

    CRUCIBLE_HOT void record(value_type value) noexcept {
        shards_[thread_shard()].record(value);
    }

    CRUCIBLE_HOT void record_on_shard(std::size_t shard, value_type value) noexcept {
        shards_[shard % ShardCount].record(value);
    }

    void merge_into(histogram_type& out) const noexcept {
        for (const auto& shard : shards_) {
            out.merge_from(shard);
        }
    }

    [[nodiscard]] std::uint64_t total_count() const noexcept {
        std::uint64_t total = 0;
        for (const auto& shard : shards_) {
            total += shard.total_count();
        }
        return total;
    }

    void reset() noexcept {
        for (auto& shard : shards_) {
            shard.reset();
        }
    }

private:
    [[nodiscard]] static std::size_t thread_shard() noexcept {
        thread_local const std::size_t shard =
            next_thread_shard_.fetch_add(1, std::memory_order_relaxed) % ShardCount;
        return shard;
    }

    alignas(64) std::array<histogram_type, ShardCount> shards_{};
    alignas(64) inline static std::atomic<std::uint64_t> next_thread_shard_{0};
};

template <typename H>
concept HdrHistogramCompatible = requires (
    H& h,
    const H& ch,
    typename H::value_type value) {
    typename H::value_type;
    { H::significant_digits } -> std::convertible_to<std::uint8_t>;
    { H::max_trackable_value } -> std::convertible_to<std::uint64_t>;
    { h.record(value) } noexcept -> std::same_as<void>;
    { ch.percentile(99.0) } noexcept -> std::same_as<std::uint64_t>;
    { ch.mean() } noexcept -> std::same_as<std::uint64_t>;
    { ch.std_dev() } noexcept -> std::same_as<std::uint64_t>;
    { ch.total_count() } noexcept -> std::same_as<std::uint64_t>;
    { h.merge_from(ch) } noexcept -> std::same_as<void>;
    { h.reset() } noexcept -> std::same_as<void>;
};

template <typename H>
concept ConcurrentHdrCompatible = requires (
    H& h,
    const H& ch,
    typename H::value_type value,
    typename H::histogram_type& out) {
    typename H::value_type;
    typename H::histogram_type;
    { h.record(value) } noexcept -> std::same_as<void>;
    { ch.total_count() } noexcept -> std::same_as<std::uint64_t>;
    { ch.merge_into(out) } noexcept -> std::same_as<void>;
    { h.reset() } noexcept -> std::same_as<void>;
};

template <typename H>
concept HdrCompatible = HdrHistogramCompatible<H> || ConcurrentHdrCompatible<H>;

template <
    std::uint8_t Significant,
    std::uint64_t MaxValue,
    std::size_t Capacity,
    typename UserTag>
using HdrRecordChannel = concurrent::PermissionedSpscChannel<
    std::uint64_t,
    Capacity,
    UserTag>;

template <HdrCompatible H, typename ConsumerHandle>
std::size_t drain_record_stream(
    H& hist,
    ConsumerHandle& consumer,
    std::size_t max_records) noexcept(noexcept(consumer.try_pop()))
{
    std::size_t drained = 0;
    while (drained < max_records) {
        auto sample = consumer.try_pop();
        if (!sample) {
            break;
        }
        hist.record(typename H::value_type{*sample});
        ++drained;
    }
    return drained;
}

}  // namespace crucible::observe
