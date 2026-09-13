#include <crucible/observe/HdrHistogram.h>

#include <atomic>
#include <array>
#include <cassert>
#include <cstdint>
#include <thread>
#include <utility>

namespace {
struct HistStreamTag {};
}  // namespace

int main() {
    using Hist = crucible::observe::HdrHistogram<2, 1'000'000>;
    static_assert(crucible::observe::HdrCompatible<Hist>);
    static_assert(Hist::bucket_slots > 0);

    Hist h;
    h.record(Hist::checked_value(10));
    h.record(Hist::checked_value(20));
    h.record(Hist::checked_value(1'000));
    h.record(Hist::checked_value(1'000'000));

    assert(h.total_count() == 4);

    // Zero is a sample like any other.  A socket with no round trip
    // yet, an idle descriptor, a queue that shed its load: each emits
    // zero as its first reading, and a range that started at one would
    // fire a contract and take the process down on that first reading.
    {
        Hist zero_h;
        zero_h.record(Hist::checked_value(0));
        assert(zero_h.total_count() == 1);
        assert(zero_h.percentile(100.0) == 0);
        assert(zero_h.mean() == 0);

        // A zero must not take probability mass from the samples
        // already recorded: it adds one to the lowest bucket and
        // changes nothing else.
        Hist mixed;
        mixed.record(Hist::checked_value(0));
        mixed.record(Hist::checked_value(0));
        mixed.record(Hist::checked_value(100));
        assert(mixed.total_count() == 3);
        // Two of the three samples are zero, so the median is zero and
        // the single positive sample is the maximum.
        assert(mixed.percentile(50.0) == 0);
        assert(mixed.percentile(100.0) >= 100);

        std::uint64_t bucket0_count = 0;
        std::uint64_t total_buckets = 0;
        mixed.for_each_nonzero([&](Hist::EncodedBucket bucket) noexcept {
            ++total_buckets;
            if (bucket.lowest_value == 0) {
                bucket0_count = bucket.count;
            }
        });
        assert(bucket0_count == 2);
        assert(total_buckets == 2);  // bucket 0 + the 100-bucket
    }
    assert(h.percentile(0.0) == 0);
    assert(h.percentile(50.0) >= 20);
    assert(h.percentile(99.0) <= Hist::max_trackable_value);
    assert(h.mean() > 0);
    assert(h.std_dev() > 0);

    // The sum of squared deviations accumulates in exact integer
    // arithmetic, not in an extended-precision float whose width
    // differs between architectures.  With every sample in one bucket
    // each deviation is zero, so the deviation has to come out exactly
    // zero rather than merely small, and the mean has to equal the one
    // value present.
    {
        Hist same;
        for (int rep = 0; rep < 1000; ++rep) {
            same.record(Hist::checked_value(777));
        }
        assert(same.total_count() == 1000);
        assert(same.std_dev() == 0);
        assert(same.mean() == same.percentile(100.0));
        assert(same.mean() == same.percentile(50.0));
        assert(same.mean() > 0);
    }

    std::uint64_t exported_count = 0;
    const std::size_t nonzero = h.for_each_nonzero([&](Hist::EncodedBucket bucket) {
        assert(bucket.count > 0);
        exported_count += bucket.count;
    });
    assert(nonzero >= 3);
    assert(exported_count == h.total_count());

    std::array<Hist::LogEncodedBucket, 2> partial_export{};
    const auto partial_result = h.serialize_log_into(partial_export);
    assert(partial_result.written == partial_export.size());
    assert(partial_result.required == nonzero);
    assert(!partial_result.complete());

    std::array<Hist::LogEncodedBucket, Hist::bucket_slots> full_export{};
    const auto full_result = h.serialize_log_into(full_export);
    assert(full_result.complete());
    assert(full_result.written == nonzero);
    std::uint64_t serialized_count = 0;
    for (std::size_t i = 0; i < full_result.written; ++i) {
        assert(full_export[i].count > 0);
        serialized_count += full_export[i].count;
    }
    assert(serialized_count == h.total_count());

    Hist delta;
    delta.record(Hist::checked_value(20));
    h.subtract_from(delta);
    assert(h.total_count() == 3);

    Hist merged;
    merged.merge_from(h);
    merged.merge_from(delta);
    assert(merged.total_count() == 4);

    // Subtraction publishes its bucket writes the same way merging
    // does.  Without that, a reader could see the lowered total while
    // the buckets it then reads still hold the old counts.
    Hist publish_h;
    for (int i = 0; i < 16; ++i) {
        publish_h.record(Hist::checked_value(500));
    }
    assert(publish_h.total_count() == 16);
    Hist publish_delta;
    for (int i = 0; i < 4; ++i) {
        publish_delta.record(Hist::checked_value(500));
    }
    publish_h.subtract_from(publish_delta);
    {
        // Read in the order a consumer does: the total first, then the
        // buckets.  The two must agree.
        const std::uint64_t post_total = publish_h.total_count();
        assert(post_total == 12);
        std::uint64_t bucket_sum = 0;
        publish_h.for_each_nonzero([&](const Hist::EncodedBucket& bucket) noexcept { bucket_sum += bucket.count; });
        assert(bucket_sum == post_total);
        assert(publish_h.percentile(50.0) >= 500);
    }

    using Concurrent = crucible::observe::ConcurrentHdrHistogram<2, 1'000'000, 2>;
    static_assert(crucible::observe::HdrCompatible<Concurrent>);

    Concurrent c;
    std::atomic<std::uint64_t> ready{0};
    std::jthread t0([&] {
        ready.fetch_add(1, std::memory_order_release);
        for (int i = 0; i < 128; ++i) {
            c.record(Concurrent::histogram_type::checked_value(100));
        }
    });
    std::jthread t1([&] {
        ready.fetch_add(1, std::memory_order_release);
        for (int i = 0; i < 128; ++i) {
            c.record(Concurrent::histogram_type::checked_value(200));
        }
    });

    while (ready.load(std::memory_order_acquire) != 2) {
        CRUCIBLE_SPIN_PAUSE;
    }
    t0.join();
    t1.join();
    assert(c.total_count() == 256);

    Hist snapshot;
    c.merge_into(snapshot);
    assert(snapshot.total_count() == 256);
    assert(snapshot.percentile(100.0) >= 200);

    c.reset();
    assert(c.total_count() == 0);

    // The tag parameter is what keeps two histograms apart.  The shard
    // a thread is assigned is cached in storage keyed by that tag, so
    // without it two histograms would share one cache and a thread
    // that landed on the first shard of one would land on the first
    // shard of the other, which is exactly the collision the sharding
    // exists to avoid.
    {
        struct LatencyHistogramTag {};
        struct DrainHistogramTag {};

        using LatencyHist = crucible::observe::ConcurrentHdrHistogram<2, 1'000'000, 4, LatencyHistogramTag>;
        using DrainHist = crucible::observe::ConcurrentHdrHistogram<2, 1'000'000, 4, DrainHistogramTag>;

        // The tag also makes the two histograms different types, so a
        // call site cannot pass one where the other belongs.
        static_assert(!std::is_same_v<LatencyHist, DrainHist>);

        LatencyHist latency;
        DrainHist drain;

        for (int i = 0; i < 64; ++i) {
            latency.record(LatencyHist::histogram_type::checked_value(100));
        }
        for (int i = 0; i < 32; ++i) {
            drain.record(DrainHist::histogram_type::checked_value(500));
        }

        assert(latency.total_count() == 64);
        assert(drain.total_count() == 32);

        // Resetting one leaves the other alone, which shared state
        // would not.
        latency.reset();
        assert(latency.total_count() == 0);
        assert(drain.total_count() == 32);
    }

    // The producers' update of the total both publishes their own
    // bucket writes and acquires the previous producer's, which is what
    // chains the publications together.  A release without the acquire
    // half breaks that chain: a reader who sees the second producer's
    // total is not thereby guaranteed to see the first producer's
    // buckets, and it reads a total larger than the counts it can find.
    //
    // On the machine this usually runs on, the read-modify-write is a
    // full barrier whichever ordering is written, so a run that passes
    // is confidence rather than proof.  The guard is still worth having
    // because it fails on a target where the distinction is real.
    {
        crucible::observe::HdrHistogram<2, 1'000'000> shared;
        std::atomic<bool> ready_flag{false};
        std::atomic<bool> stop_flag{false};
        std::atomic<std::uint64_t> torn_observations{0};
        std::atomic<std::uint64_t> reader_iterations{0};

        constexpr std::size_t kProducerCount = 4;
        constexpr std::size_t kSamplesPerProducer = 8192;
        using HRegress = decltype(shared);

        std::array<std::jthread, kProducerCount> producers{};
        for (std::size_t p = 0; p < kProducerCount; ++p) {
            producers[p] = std::jthread{[&shared, &ready_flag, p] {
                while (!ready_flag.load(std::memory_order_acquire)) {
                    CRUCIBLE_SPIN_PAUSE;
                }
                for (std::size_t i = 0; i < kSamplesPerProducer; ++i) {
                    const std::uint64_t v = (static_cast<std::uint64_t>(p) * 1000) + ((i % 90) + 10);
                    shared.record(HRegress::checked_value(v));
                }
            }};
        }

        std::jthread reader{[&shared, &ready_flag, &stop_flag, &torn_observations, &reader_iterations] {
            while (!ready_flag.load(std::memory_order_acquire)) {
                CRUCIBLE_SPIN_PAUSE;
            }
            while (!stop_flag.load(std::memory_order_acquire)) {
                const std::uint64_t total = shared.total_count();
                if (total == 0) {
                    continue;
                }
                std::uint64_t bucket_sum = 0;
                shared.for_each_nonzero(
                    [&](const HRegress::EncodedBucket& bucket) noexcept { bucket_sum += bucket.count; });
                // The comparison is deliberately one-sided.  Counting
                // more than the total is normal, because a producer
                // writes its bucket before it raises the total.  The
                // other direction is the defect.
                if (bucket_sum < total) {
                    torn_observations.fetch_add(1, std::memory_order_relaxed);
                }
                reader_iterations.fetch_add(1, std::memory_order_relaxed);
            }
        }};

        ready_flag.store(true, std::memory_order_release);
        for (auto& t : producers) {
            t.join();
        }
        stop_flag.store(true, std::memory_order_release);
        reader.join();

        assert(shared.total_count() == kProducerCount * kSamplesPerProducer);
        // A reader that never got past the empty-histogram check would
        // report zero violations without having looked at anything.
        assert(reader_iterations.load(std::memory_order_relaxed) > 0);
        assert(torn_observations.load(std::memory_order_relaxed) == 0);
    }

    using Channel = crucible::observe::HdrRecordChannel<2, 1'000'000, 8, HistStreamTag>;
    Channel channel;
    auto whole = crucible::safety::mint_permission_root<typename Channel::whole_tag>();
    auto [producer_perm, consumer_perm] =
        crucible::safety::mint_permission_split<typename Channel::producer_tag, typename Channel::consumer_tag>(
            std::move(whole));
    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));

    assert(producer.try_push(42));
    assert(producer.try_push(84));

    Hist streamed;
    assert(crucible::observe::drain_record_stream(streamed, consumer, 8) == 2);
    assert(streamed.total_count() == 2);
    assert(streamed.percentile(100.0) >= 84);
}
