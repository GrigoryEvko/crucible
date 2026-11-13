#pragma once

#include <fixy/Core.h>
#include <fixy/Refined.h>
#include <fixy/concurrent/PermissionedSpscChannel.h>
#include <foundation/Platform.h>
#include <foundation/ThreadLocalRef.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
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
    return std::uint64_t{1} << ::fixy::bit_width(x - 1);
}

template <std::uint8_t Significant, std::uint64_t MaxValue>
struct HdrLayout {
    static_assert(Significant >= 1 && Significant <= 5, "HdrHistogram Significant must be in [1, 5]");
    static_assert(MaxValue > 0, "HdrHistogram MaxValue must be positive");

    static constexpr std::uint64_t sub_bucket_count = next_power_of_two(pow10_u64(Significant) * 2);
    static constexpr std::uint64_t sub_bucket_half_count = sub_bucket_count / 2;
    static constexpr unsigned sub_bucket_half_count_magnitude =
        static_cast<unsigned>(::fixy::countr_zero(sub_bucket_half_count));
    static constexpr std::uint64_t sub_bucket_mask = sub_bucket_count - 1;

    [[nodiscard]] static constexpr std::uint32_t bucket_index(std::uint64_t value) noexcept {
        const std::uint64_t normalized = value | sub_bucket_mask;
        const auto bit_count = static_cast<unsigned>(::fixy::bit_width(normalized));
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
    using value_type = ::fixy::Refined<::fixy::in_range<std::uint64_t{0}, MaxValue>, std::uint64_t>;

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

    // The one door into value_type.  The predicate runs here, so a sample
    // above MaxValue stops at the boundary rather than in a bucket index.
    [[nodiscard]] static constexpr value_type checked_value(std::uint64_t value) noexcept {
        return ::fixy::mint_refined<::fixy::in_range<std::uint64_t{0}, MaxValue>>(value);
    }

    // The slot of each value of value_type is below bucket_slots: the slot
    // grows with the value, and a static_assert bounds the slot of MaxValue.
    // Thus the clamp changes no slot.  The clamp lets the optimizer prove the
    // bound, so the check of the Index costs no branch.
    CRUCIBLE_HOT void record(value_type value) noexcept {
        const std::size_t slot = ::fixy::min(layout_type::counts_index(value.value()), bucket_slots - 1);
        static_cast<void>(
            counts_.at(::fixy::Index<bucket_slots>::check(slot).expect("a clamped slot is below bucket_slots"))
                .fetch_add_acq_rel(1));
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
        static_cast<void>(total_count_.fetch_add_acq_rel(1));
    }

    [[nodiscard]] std::uint64_t total_count() const noexcept { return total_count_.load_acquire(); }

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
        // IEEE-754 double multiply and divide are correctly rounded and
        // bit-identical on every target.
        //
        // rank_f is in [0, total], so the conversion truncates it toward zero
        // with no overflow.  A fraction above that whole number adds one, so
        // rank is the ceiling of rank_f, with no call of the math library.
        const double rank_f = (pct / 100.0) * static_cast<double>(total);
        std::uint64_t rank = static_cast<std::uint64_t>(rank_f);
        if (static_cast<double>(rank) < rank_f) {
            ++rank;
        }
        if (rank == 0) {
            rank = 1;
        }

        std::uint64_t seen = 0;
        for (auto const [index, count] : counts_.enumerate()) {
            seen += count->load_acquire();
            if (seen >= rank) {
                return layout_type::value_from_index(index.raw());
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
        for (auto const [index, cell] : counts_.enumerate()) {
            const std::uint64_t count = cell->load_acquire();
            if (count != 0) {
                sum += static_cast<wide_unsigned>(layout_type::value_from_index(index.raw()))
                     * static_cast<wide_unsigned>(count);
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
        for (auto const [index, cell] : counts_.enumerate()) {
            const std::uint64_t count = cell->load_acquire();
            if (count != 0) {
                const wide_signed delta = static_cast<wide_signed>(layout_type::value_from_index(index.raw()))
                                        - static_cast<wide_signed>(avg);
                const wide_unsigned sq = static_cast<wide_unsigned>(delta * delta);
                sum_sq += sq * static_cast<wide_unsigned>(count);
            }
        }
        const double variance = static_cast<double>(sum_sq) / static_cast<double>(total);
        return static_cast<std::uint64_t>(::fixy::fp::sqrt(variance) + 0.5);
    }

    void merge_from(const HdrHistogram& other) noexcept {
        for (auto const [index, cell] : other.counts_.enumerate()) {
            const std::uint64_t count = cell->load_acquire();
            if (count != 0) {
                static_cast<void>(counts_.at(index).fetch_add_acq_rel(count));
            }
        }
        // acq_rel for the same reason as record. Concurrent merges into one
        // histogram form a read-modify-write chain on total_count_, and the
        // acquire half is what publishes the bucket writes along it.
        static_cast<void>(total_count_.fetch_add_acq_rel(other.total_count()));
    }

    void add_from(const HdrHistogram& other) noexcept { merge_from(other); }

    // acq_rel for the same reason as record. Two concurrent subtracts, or a
    // subtract racing a merge, form a read-modify-write chain on
    // total_count_ whose success ordering decides what the chain publishes.
    void subtract_from(const HdrHistogram& other) noexcept {
        for (auto const [index, cell] : other.counts_.enumerate()) {
            const std::uint64_t count = cell->load_acquire();
            if (count != 0) {
                saturating_sub(counts_.at(index), count);
            }
        }
        saturating_sub(total_count_, other.total_count());
    }

    void reset() noexcept {
        for (auto& count : counts_) {
            count.store_release(0);
        }
        total_count_.store_release(0);
    }

    template <typename Fn>
    std::size_t for_each_nonzero(Fn&& fn) const {
        std::size_t emitted = 0;
        for (auto const [index, cell] : counts_.enumerate()) {
            const std::uint64_t count = cell->load_acquire();
            if (count != 0) {
                fn(EncodedBucket{
                    .index = static_cast<std::uint32_t>(index.raw()),
                    .lowest_value = layout_type::value_from_index(index.raw()),
                    .count = count,
                });
                ++emitted;
            }
        }
        return emitted;
    }

    // Writes each nonzero bucket that fits into out, in slot order, and
    // counts each nonzero bucket.
    SerializeResult serialize_log_into(::fixy::View<LogEncodedBucket> out) const noexcept {
        SerializeResult result{.written = 0, .required = 0};
        for (auto const [index, cell] : counts_.enumerate()) {
            const std::uint64_t count = cell->load_acquire();
            if (count == 0) {
                continue;
            }
            out.get(result.written)
                .match(
                    [&](auto slot) noexcept {
                        *slot = LogEncodedBucket{
                            .index = static_cast<std::uint32_t>(index.raw()),
                            .count = count,
                        };
                        ++result.written;
                    },
                    []() noexcept {});
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

    // Subtracts amount from the cell, and stops at zero.  A refused
    // compare-exchange gives the value that the cell held, and the next turn
    // starts from it.
    static void saturating_sub(::fixy::Atomic<std::uint64_t>& dst, std::uint64_t amount) noexcept {
        std::uint64_t observed = dst.load_acquire();
        bool is_done = false;
        while (!is_done) {
            const std::uint64_t desired = observed > amount ? observed - amount : 0;
            is_done = dst.cas_acq_rel(observed, desired)
                          .match([](::fixy::Unit) noexcept { return true; },
                                 [&observed](::fixy::CasRefusal<std::uint64_t> refusal) noexcept {
                                     observed = refusal.observed;
                                     return false;
                                 });
        }
    }

    alignas(64)::foundation::core::FixedArray<::fixy::Atomic<std::uint64_t>, bucket_slots> counts_{};
    alignas(64)::fixy::Atomic<std::uint64_t> total_count_{};
};

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

    [[nodiscard]] static constexpr value_type checked_value(std::uint64_t value) noexcept {
        return histogram_type::checked_value(value);
    }

    CRUCIBLE_HOT void record(value_type value) noexcept { shards_.at(thread_shard_()).record(value); }

    CRUCIBLE_HOT void record_on_shard(std::size_t shard, value_type value) noexcept {
        shards_.at(ShardIndex::check(shard % ShardCount).expect("a shard modulo ShardCount is below ShardCount"))
            .record(value);
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
    using ShardIndex = ::fixy::Index<ShardCount>;

    // The cell stores the shard index plus one. Zero-initialized storage
    // then reads as "this thread has picked no shard yet", because a real
    // index plus one is never zero.  A cell that is not zero holds at most
    // ShardCount, so the clamp changes no shard.  The clamp lets the
    // optimizer prove the bound, so the check of the Index costs no branch.
    [[nodiscard]] CRUCIBLE_HOT ShardIndex thread_shard_() noexcept {
        const ::foundation::ThreadLocalRef<UniqueTag, std::size_t> cell{};
        std::size_t& cached_plus_one = cell.peek_mut();
        if (cached_plus_one == 0) [[unlikely]] {
            cached_plus_one = (next_thread_shard_.fetch_add_acq_rel(1) % ShardCount) + 1;
        }
        return ShardIndex::check(::fixy::min(cached_plus_one - 1, ShardCount - 1))
            .expect("a clamped shard is below ShardCount");
    }

    alignas(64)::foundation::core::FixedArray<histogram_type, ShardCount> shards_{};
    alignas(64)::fixy::Atomic<std::uint64_t> next_thread_shard_{};
};

template <typename H>
concept HdrHistogramCompatible = requires(H& h, const H& ch, typename H::value_type value) {
    typename H::value_type;
    { H::checked_value(std::uint64_t{}) } noexcept -> std::same_as<typename H::value_type>;
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
        { H::checked_value(std::uint64_t{}) } noexcept -> std::same_as<typename H::value_type>;
        { h.record(value) } noexcept -> std::same_as<void>;
        { ch.total_count() } noexcept -> std::same_as<std::uint64_t>;
        { ch.merge_into(out) } noexcept -> std::same_as<void>;
        { h.reset() } noexcept -> std::same_as<void>;
    };

template <typename H>
concept HdrCompatible = HdrHistogramCompatible<H> || ConcurrentHdrCompatible<H>;

// The channel carries the brand of the root that its permissions grow
// from, as fixy/concurrent/PermissionedSpscChannel.h says.
template <std::uint8_t Significant, std::uint64_t MaxValue, std::size_t Capacity, typename UserTag, typename Brand>
using HdrRecordChannel = ::fixy::concurrent::PermissionedSpscChannel<std::uint64_t, Capacity, UserTag, Brand>;

template <HdrCompatible H, typename ConsumerHandle>
std::size_t drain_record_stream(H& hist, ConsumerHandle& consumer,
                                std::size_t max_records) noexcept(noexcept(consumer.try_pop())) {
    std::size_t drained = 0;
    while (drained < max_records) {
        auto sample = consumer.try_pop();
        if (sample.is_none()) {
            break;
        }
        hist.record(H::checked_value(std::move(sample).expect("the test above found a sample")));
        ++drained;
    }
    return drained;
}

}  // namespace crucible::observe
