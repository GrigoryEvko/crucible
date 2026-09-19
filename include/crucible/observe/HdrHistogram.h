#pragma once

#include <crucible/Platform.h>
#include <crucible/concurrent/PermissionedSpscChannel.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/ThreadLocalRef.h>

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
    static_assert(Significant >= 1 && Significant <= 5, "HdrHistogram Significant must be in [1, 5]");
    static_assert(MaxValue > 0, "HdrHistogram MaxValue must be positive");

    static constexpr std::uint64_t sub_bucket_count = next_power_of_two(pow10_u64(Significant) * 2);
    static constexpr std::uint64_t sub_bucket_half_count = sub_bucket_count / 2;
    static constexpr unsigned sub_bucket_half_count_magnitude = std::countr_zero(sub_bucket_half_count);
    static constexpr std::uint64_t sub_bucket_mask = sub_bucket_count - 1;

    [[nodiscard]] static constexpr std::uint32_t bucket_index(std::uint64_t value) noexcept {
        const std::uint64_t normalized = value | sub_bucket_mask;
        const auto bit_count = static_cast<unsigned>(std::bit_width(normalized));
        return bit_count > sub_bucket_half_count_magnitude + 1 ? bit_count - sub_bucket_half_count_magnitude - 1 : 0;
    }

    static constexpr std::uint32_t bucket_count = bucket_index(MaxValue) + 1;
    static constexpr std::size_t counts_len =
        static_cast<std::size_t>(bucket_count + 1) * static_cast<std::size_t>(sub_bucket_half_count);

    [[nodiscard]] static constexpr std::size_t counts_index(std::uint64_t value) noexcept {
        const std::uint32_t bucket = bucket_index(value);
        if (bucket == 0) {
            return static_cast<std::size_t>(value);
        }
        const std::uint64_t sub_bucket = value >> bucket;
        const std::uint64_t offset = sub_bucket - sub_bucket_half_count;
        return (static_cast<std::size_t>(bucket + 1) * static_cast<std::size_t>(sub_bucket_half_count))
             + static_cast<std::size_t>(offset);
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

template <std::uint8_t Significant = 3, std::uint64_t MaxValue = 3600000000000ull>
class HdrHistogram {
public:
    using layout_type = detail::HdrLayout<Significant, MaxValue>;
    // Zero is a first-class sample, so the lower bound is 0 and not 1.
    // Transports emit zero on cold paths: round-trip time before the first
    // round trip, bandwidth before the first byte, queue depth on an idle
    // descriptor. Excluding zero would need a sentinel bucket or a guard at
    // every call site.
    using value_type = safety::Refined<safety::in_range<std::uint64_t{0}, MaxValue>, std::uint64_t>;

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

        [[nodiscard]] constexpr bool complete() const noexcept { return written == required; }
    };

    static constexpr std::uint8_t significant_digits = Significant;
    static constexpr std::uint64_t max_trackable_value = MaxValue;
    static constexpr std::size_t bucket_slots = layout_type::counts_len;
    static_assert(layout_type::counts_index(std::uint64_t{0}) < bucket_slots,
                  "bucket 0 must be a valid slot. Zero samples are first-class "
                  "inputs.");
    static_assert(layout_type::counts_index(std::uint64_t{1}) < bucket_slots);
    static_assert(layout_type::counts_index(MaxValue) < bucket_slots);

    HdrHistogram() = default;
    HdrHistogram(const HdrHistogram&) = delete;
    HdrHistogram& operator=(const HdrHistogram&) = delete;

    [[nodiscard]] static constexpr value_type checked_value(std::uint64_t value) noexcept { return value_type{value}; }

    CRUCIBLE_HOT void record(value_type value) noexcept {
        const std::size_t index = layout_type::counts_index(value.value());
        counts_[index].fetch_add(1, std::memory_order_relaxed);
        // acq_rel, not release. A release-only read-modify-write does not
        // synchronize with a second producer's release-only one. Producer Y
        // reads producer X's incremented value, but the read half of Y's
        // operation has no acquire semantics, so X's earlier bucket stores
        // do not happen before Y's release. A reader that acquires Y's
        // release then sees Y's bucket updates but not X's, and the bucket
        // sum falls short of total_count_. The percentile loop runs off the
        // end and returns MaxValue. acq_rel builds the transitive chain from
        // X's stores through X's release, Y's acquiring read-modify-write
        // and Y's release to the reader's acquire.
        total_count_.fetch_add(1, std::memory_order_acq_rel);
    }

    [[nodiscard]] std::uint64_t total_count() const noexcept { return total_count_.load(std::memory_order_acquire); }

    [[nodiscard]] std::uint64_t percentile(double pct) const noexcept {
        const std::uint64_t total = total_count();
        if (total == 0 || !(pct > 0.0)) {
            return 0;
        }
        if (pct > 100.0) {
            pct = 100.0;
        }

        // The rank is computed in double, never long double. long double is
        // 80-bit on x86 and 64-bit on aarch64, so one pct and total pair
        // could pick a different bucket near a boundary on different hosts.
        // IEEE-754 double multiply, divide and ceil are correctly rounded
        // and bit-identical on every target.
        const double rank_f = (pct / 100.0) * static_cast<double>(total);
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

        // The accumulation is exact integer arithmetic with no float at all.
        // A float sum loses bits once it passes the mantissa, and long
        // double differs in width between x86 and aarch64. value_from_index
        // times count is exact in 128 bits, and MaxValue times a realistic
        // sample count stays far below 2^128.
        wide_unsigned sum = 0;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = counts_[i].load(std::memory_order_relaxed);
            if (count != 0) {
                sum += static_cast<wide_unsigned>(layout_type::value_from_index(i)) * static_cast<wide_unsigned>(count);
            }
        }
        const wide_unsigned total128 = static_cast<wide_unsigned>(total);
        return static_cast<std::uint64_t>((sum + total128 / 2) / total128);
    }

    [[nodiscard]] std::uint64_t std_dev() const noexcept {
        const std::uint64_t total = total_count();
        if (total == 0) {
            return 0;
        }

        // The deviation form sums (value - mean) squared. It is free of the
        // catastrophic cancellation the sum-of-squares form suffers. The
        // accumulation is exact in 128 bits. The final divide and sqrt are
        // both correctly rounded, so the result is bit-identical on every
        // target.
        const std::uint64_t avg = mean();
        wide_unsigned sum_sq = 0;
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = counts_[i].load(std::memory_order_relaxed);
            if (count != 0) {
                const wide_signed delta =
                    static_cast<wide_signed>(layout_type::value_from_index(i)) - static_cast<wide_signed>(avg);
                const wide_unsigned sq = static_cast<wide_unsigned>(delta * delta);
                sum_sq += sq * static_cast<wide_unsigned>(count);
            }
        }
        const double variance = static_cast<double>(sum_sq) / static_cast<double>(total);
        return static_cast<std::uint64_t>(std::sqrt(variance) + 0.5);
    }

    void merge_from(const HdrHistogram& other) noexcept {
        for (std::size_t i = 0; i < counts_.size(); ++i) {
            const std::uint64_t count = other.counts_[i].load(std::memory_order_relaxed);
            if (count != 0) {
                counts_[i].fetch_add(count, std::memory_order_relaxed);
            }
        }
        // acq_rel for the same reason as record. Concurrent merges into one
        // histogram form a read-modify-write chain on total_count_, and the
        // acquire half is what publishes the bucket writes along it.
        total_count_.fetch_add(other.total_count(), std::memory_order_acq_rel);
    }

    void add_from(const HdrHistogram& other) noexcept { merge_from(other); }

    // acq_rel for the same reason as record. Two concurrent subtracts, or a
    // subtract racing a merge, form a read-modify-write chain on
    // total_count_ whose success ordering decides what the chain publishes.
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
    // __extension__ suppresses -Wpedantic on the __int128 keyword. Every use
    // below names these aliases and never respells the keyword, so the rest
    // of the header stays pedantic-clean.
    __extension__ using wide_unsigned = unsigned __int128;
    __extension__ using wide_signed = __int128;

    static void saturating_sub(std::atomic<std::uint64_t>& dst, std::uint64_t amount,
                               std::memory_order success = std::memory_order_relaxed) noexcept {
        std::uint64_t observed = dst.load(std::memory_order_relaxed);
        while (true) {
            const std::uint64_t desired = observed > amount ? observed - amount : 0;
            if (dst.compare_exchange_weak(observed, desired, success, std::memory_order_relaxed)) {
                return;
            }
        }
    }

    alignas(64) std::array<std::atomic<std::uint64_t>, bucket_slots> counts_{};
    alignas(64) std::atomic<std::uint64_t> total_count_{0};
};

// A standard library may substitute mutex-backed operations where the target
// lacks the intrinsic, silently putting a lock inside every record. Refuse to
// build instead.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this target.");

// UniqueTag is mandatory. The per-thread shard cell is keyed on the tag, so
// two instances sharing a tag also share one cell per thread and draw shards
// from one interleaved stream. Each call site declares its own tag type.

template <std::uint8_t Significant = 3, std::uint64_t MaxValue = 3600000000000ull, std::size_t ShardCount = 4,
          typename UniqueTag = struct DefaultConcurrentHdrTag>
class ConcurrentHdrHistogram {
    static_assert(ShardCount > 0, "ConcurrentHdrHistogram needs at least one shard");

public:
    using histogram_type = HdrHistogram<Significant, MaxValue>;
    using value_type = typename histogram_type::value_type;
    using unique_tag = UniqueTag;

    ConcurrentHdrHistogram() = default;
    ConcurrentHdrHistogram(const ConcurrentHdrHistogram&) = delete;
    ConcurrentHdrHistogram& operator=(const ConcurrentHdrHistogram&) = delete;

    CRUCIBLE_HOT void record(value_type value) noexcept { shards_[thread_shard_()].record(value); }

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
    // The cell stores the shard index plus one. Zero-initialized storage
    // then reads as "this thread has picked no shard yet", because a real
    // index plus one is never zero.
    [[nodiscard]] CRUCIBLE_HOT std::size_t thread_shard_() noexcept {
        const ::crucible::safety::ThreadLocalRef<UniqueTag, std::size_t> cell{};
        std::size_t& cached_plus_one = cell.peek_mut();
        if (cached_plus_one == 0) [[unlikely]] {
            cached_plus_one = (next_thread_shard_.fetch_add(1, std::memory_order_relaxed) % ShardCount) + 1;
        }
        return cached_plus_one - 1;
    }

    alignas(64) std::array<histogram_type, ShardCount> shards_{};
    alignas(64) std::atomic<std::uint64_t> next_thread_shard_{0};
};

template <typename H>
concept HdrHistogramCompatible = requires(H& h, const H& ch, typename H::value_type value) {
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
concept ConcurrentHdrCompatible =
    requires(H& h, const H& ch, typename H::value_type value, typename H::histogram_type& out) {
        typename H::value_type;
        typename H::histogram_type;
        { h.record(value) } noexcept -> std::same_as<void>;
        { ch.total_count() } noexcept -> std::same_as<std::uint64_t>;
        { ch.merge_into(out) } noexcept -> std::same_as<void>;
        { h.reset() } noexcept -> std::same_as<void>;
    };

template <typename H>
concept HdrCompatible = HdrHistogramCompatible<H> || ConcurrentHdrCompatible<H>;

template <std::uint8_t Significant, std::uint64_t MaxValue, std::size_t Capacity, typename UserTag>
using HdrRecordChannel = concurrent::PermissionedSpscChannel<std::uint64_t, Capacity, UserTag>;

template <HdrCompatible H, typename ConsumerHandle>
std::size_t drain_record_stream(H& hist, ConsumerHandle& consumer,
                                std::size_t max_records) noexcept(noexcept(consumer.try_pop())) {
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
