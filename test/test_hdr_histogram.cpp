#include <crucible/observe/HdrHistogram.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

// The tag of a concurrent histogram keys the cell of each thread, which needs
// a stable identity, so the tags stand in a named namespace.
namespace hdr_histogram_test {

struct LatencyHistogramTag {};
struct DrainHistogramTag {};

}  // namespace hdr_histogram_test

namespace {
struct HistStreamTag {};

// When the reader thread starts, relative to the producer threads.  The
// scheduler selects the order, so each check of the reader must hold in
// each order.
enum class ReaderStart : std::uint8_t {
    WithProducers,
    AfterProducers,
};

// Runs the bodies, each on a thread of its own, and joins each thread.  The
// bodies share a histogram on purpose, so they go through the raw probe of
// the thread door.
template <typename... Bodies>
void run_threads(Bodies&... bodies) noexcept {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    ::fixy::thread::run_joined_raw_probe(ctx, kTest, "the threads of the test share a histogram on purpose",
                                         ::fixy::thread::ThreadOptions{}, bodies...)
        .expect("the thread door starts each thread of the test");
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
void check_published_buckets(ReaderStart reader_start) {
    crucible::observe::HdrHistogram<2, 1000000> shared;
    ::fixy::Atomic<bool> ready_flag{false};
    ::fixy::Atomic<bool> stop_flag{false};
    ::fixy::Atomic<std::uint64_t, ::fixy::Counter> torn_observations{0};
    ::fixy::Atomic<std::uint32_t> finished_producers{0};
    std::uint64_t last_total_seen = 0;

    constexpr std::size_t kProducerCount = 4;
    constexpr std::size_t kSamplesPerProducer = 8192;
    using HRegress = decltype(shared);
    using ProducerIndex = ::fixy::Index<kProducerCount>;

    auto read_until_stopped = [&shared, &ready_flag, &stop_flag, &torn_observations, &last_total_seen] noexcept {
        while (!ready_flag.load_acquire()) {
            CRUCIBLE_SPIN_PAUSE;
        }
        // The reader reads the stop flag before each iteration.  The first
        // iteration that finds the flag set is the last one.  It starts
        // after each producer thread stopped, so it sees every sample.
        // When the reader starts after the producers stop, that iteration
        // is the only one.
        bool was_stopped = false;
        while (!was_stopped) {
            was_stopped = stop_flag.load_acquire();
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
                static_cast<void>(torn_observations.fetch_add_relaxed(1));
            }
            last_total_seen = total;
        }
    };

    auto producer_of = [&shared, &ready_flag, &finished_producers](ProducerIndex producer) noexcept {
        return [&shared, &ready_flag, &finished_producers, producer] noexcept {
            while (!ready_flag.load_acquire()) {
                CRUCIBLE_SPIN_PAUSE;
            }
            for (std::size_t i = 0; i < kSamplesPerProducer; ++i) {
                const std::uint64_t sample_value = (producer.raw() * 1000) + ((i % 90) + 10);
                shared.record(HRegress::checked_value(sample_value));
            }
            static_cast<void>(finished_producers.fetch_add_acq_rel(1));
        };
    };
    auto producer_0 = producer_of(::fixy::index<0>());
    auto producer_1 = producer_of(::fixy::index<1>());
    auto producer_2 = producer_of(::fixy::index<2>());
    auto producer_3 = producer_of(::fixy::index<3>());

    // The conductor opens the start of the producers, waits until each
    // producer ends, and then sets the stop flag of the reader.  Each
    // producer counts its end with a release, and the conductor reads the
    // count with an acquire load.  So the stop flag orders each sample
    // before the last iteration of the reader, as the join of each
    // producer did.
    auto conductor = [&ready_flag, &stop_flag, &finished_producers] noexcept {
        ready_flag.store_release(true);
        while (finished_producers.load_acquire() < kProducerCount) {
            CRUCIBLE_SPIN_PAUSE;
        }
        stop_flag.store_release(true);
    };
    if (reader_start == ReaderStart::WithProducers) {
        run_threads(producer_0, producer_1, producer_2, producer_3, read_until_stopped, conductor);
    } else {
        run_threads(producer_0, producer_1, producer_2, producer_3, conductor);
        run_threads(read_until_stopped);
    }

    CRUCIBLE_TEST_ASSERT(kTest, shared.total_count() == kProducerCount * kSamplesPerProducer);
    // The last iteration of the reader read the buckets of the full
    // histogram.  A reader that stops at the empty-histogram check reads
    // no bucket, and it finds no torn state for that reason.
    CRUCIBLE_TEST_ASSERT(kTest, last_total_seen == kProducerCount * kSamplesPerProducer);
    CRUCIBLE_TEST_ASSERT(kTest, torn_observations.load_acquire() == 0);
}
}  // namespace

int main() {
    using Hist = crucible::observe::HdrHistogram<2, 1000000>;
    static_assert(crucible::observe::HdrCompatible<Hist>);
    static_assert(Hist::bucket_slots > 0);

    Hist h;
    h.record(Hist::checked_value(10));
    h.record(Hist::checked_value(20));
    h.record(Hist::checked_value(1000));
    h.record(Hist::checked_value(1000000));

    CRUCIBLE_TEST_ASSERT(kTest, h.total_count() == 4);

    // Zero is a sample like any other.  A socket with no round trip
    // yet, an idle descriptor, a queue that shed its load: each emits
    // zero as its first reading, and a range that started at one would
    // fire a contract and take the process down on that first reading.
    {
        Hist zero_h;
        zero_h.record(Hist::checked_value(0));
        CRUCIBLE_TEST_ASSERT(kTest, zero_h.total_count() == 1);
        CRUCIBLE_TEST_ASSERT(kTest, zero_h.percentile(100.0) == 0);
        CRUCIBLE_TEST_ASSERT(kTest, zero_h.mean() == 0);

        // A zero must not take probability mass from the samples
        // already recorded: it adds one to the lowest bucket and
        // changes nothing else.
        Hist mixed;
        mixed.record(Hist::checked_value(0));
        mixed.record(Hist::checked_value(0));
        mixed.record(Hist::checked_value(100));
        CRUCIBLE_TEST_ASSERT(kTest, mixed.total_count() == 3);
        // Two of the three samples are zero, so the median is zero and
        // the single positive sample is the maximum.
        CRUCIBLE_TEST_ASSERT(kTest, mixed.percentile(50.0) == 0);
        CRUCIBLE_TEST_ASSERT(kTest, mixed.percentile(100.0) >= 100);

        std::uint64_t bucket0_count = 0;
        std::uint64_t total_buckets = 0;
        mixed.for_each_nonzero([&](Hist::EncodedBucket bucket) noexcept {
            ++total_buckets;
            if (bucket.lowest_value == 0) {
                bucket0_count = bucket.count;
            }
        });
        CRUCIBLE_TEST_ASSERT(kTest, bucket0_count == 2);
        CRUCIBLE_TEST_ASSERT(kTest, total_buckets == 2);  // bucket 0 + the 100-bucket
    }
    CRUCIBLE_TEST_ASSERT(kTest, h.percentile(0.0) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, h.percentile(50.0) >= 20);
    CRUCIBLE_TEST_ASSERT(kTest, h.percentile(99.0) <= Hist::max_trackable_value);
    CRUCIBLE_TEST_ASSERT(kTest, h.mean() > 0);
    CRUCIBLE_TEST_ASSERT(kTest, h.std_dev() > 0);

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
        CRUCIBLE_TEST_ASSERT(kTest, same.total_count() == 1000);
        CRUCIBLE_TEST_ASSERT(kTest, same.std_dev() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, same.mean() == same.percentile(100.0));
        CRUCIBLE_TEST_ASSERT(kTest, same.mean() == same.percentile(50.0));
        CRUCIBLE_TEST_ASSERT(kTest, same.mean() > 0);
    }

    std::uint64_t exported_count = 0;
    const std::size_t nonzero = h.for_each_nonzero([&](Hist::EncodedBucket bucket) {
        CRUCIBLE_TEST_ASSERT(kTest, bucket.count > 0);
        exported_count += bucket.count;
    });
    CRUCIBLE_TEST_ASSERT(kTest, nonzero >= 3);
    CRUCIBLE_TEST_ASSERT(kTest, exported_count == h.total_count());

    ::foundation::core::FixedArray<Hist::LogEncodedBucket, 2> partial_export{};
    const auto partial_result =
        partial_export.with_mut([&h](auto run) noexcept { return h.serialize_log_into(run.view()); });
    CRUCIBLE_TEST_ASSERT(kTest, partial_result.written == partial_export.size());
    CRUCIBLE_TEST_ASSERT(kTest, partial_result.required == nonzero);
    CRUCIBLE_TEST_ASSERT(kTest, !partial_result.complete());

    ::foundation::core::FixedArray<Hist::LogEncodedBucket, Hist::bucket_slots> full_export{};
    const auto full_result = full_export.with_mut([&h](auto run) noexcept { return h.serialize_log_into(run.view()); });
    CRUCIBLE_TEST_ASSERT(kTest, full_result.complete());
    CRUCIBLE_TEST_ASSERT(kTest, full_result.written == nonzero);
    std::uint64_t serialized_count = 0;
    for (std::size_t i = 0; i < full_result.written; ++i) {
        const std::uint64_t count = full_export.get(i).expect("the position is below the written count")->count;
        CRUCIBLE_TEST_ASSERT(kTest, count > 0);
        serialized_count += count;
    }
    CRUCIBLE_TEST_ASSERT(kTest, serialized_count == h.total_count());

    Hist delta;
    delta.record(Hist::checked_value(20));
    h.subtract_from(delta);
    CRUCIBLE_TEST_ASSERT(kTest, h.total_count() == 3);

    Hist merged;
    merged.merge_from(h);
    merged.merge_from(delta);
    CRUCIBLE_TEST_ASSERT(kTest, merged.total_count() == 4);

    // Subtraction publishes its bucket writes the same way merging
    // does.  Without that, a reader could see the lowered total while
    // the buckets it then reads still hold the old counts.
    Hist publish_h;
    for (int i = 0; i < 16; ++i) {
        publish_h.record(Hist::checked_value(500));
    }
    CRUCIBLE_TEST_ASSERT(kTest, publish_h.total_count() == 16);
    Hist publish_delta;
    for (int i = 0; i < 4; ++i) {
        publish_delta.record(Hist::checked_value(500));
    }
    publish_h.subtract_from(publish_delta);
    {
        // Read in the order a consumer does: the total first, then the
        // buckets.  The two must agree.
        const std::uint64_t post_total = publish_h.total_count();
        CRUCIBLE_TEST_ASSERT(kTest, post_total == 12);
        std::uint64_t bucket_sum = 0;
        publish_h.for_each_nonzero([&](const Hist::EncodedBucket& bucket) noexcept { bucket_sum += bucket.count; });
        CRUCIBLE_TEST_ASSERT(kTest, bucket_sum == post_total);
        CRUCIBLE_TEST_ASSERT(kTest, publish_h.percentile(50.0) >= 500);
    }

    using Concurrent = crucible::observe::ConcurrentHdrHistogram<2, 1000000, 2>;
    static_assert(crucible::observe::HdrCompatible<Concurrent>);

    Concurrent c;
    ::fixy::Atomic<std::uint64_t> ready{0};
    auto t0 = [&c, &ready] noexcept {
        static_cast<void>(ready.fetch_add_acq_rel(1));
        for (int i = 0; i < 128; ++i) {
            c.record(Concurrent::histogram_type::checked_value(100));
        }
    };
    auto t1 = [&c, &ready] noexcept {
        static_cast<void>(ready.fetch_add_acq_rel(1));
        for (int i = 0; i < 128; ++i) {
            c.record(Concurrent::histogram_type::checked_value(200));
        }
    };
    // The waiter waits until the two recorders run, as the main thread did
    // before it joined them.
    auto waiter = [&ready] noexcept {
        while (ready.load_acquire() != 2) {
            CRUCIBLE_SPIN_PAUSE;
        }
    };
    run_threads(t0, t1, waiter);
    CRUCIBLE_TEST_ASSERT(kTest, c.total_count() == 256);

    Hist snapshot;
    c.merge_into(snapshot);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.total_count() == 256);
    CRUCIBLE_TEST_ASSERT(kTest, snapshot.percentile(100.0) >= 200);

    c.reset();
    CRUCIBLE_TEST_ASSERT(kTest, c.total_count() == 0);

    // The tag parameter is what keeps two histograms apart.  The shard
    // a thread is assigned is cached in storage keyed by that tag, so
    // without it two histograms would share one cache and a thread
    // that landed on the first shard of one would land on the first
    // shard of the other, which is exactly the collision the sharding
    // exists to avoid.
    {
        using LatencyHist =
            crucible::observe::ConcurrentHdrHistogram<2, 1000000, 4, hdr_histogram_test::LatencyHistogramTag>;
        using DrainHist =
            crucible::observe::ConcurrentHdrHistogram<2, 1000000, 4, hdr_histogram_test::DrainHistogramTag>;

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

        CRUCIBLE_TEST_ASSERT(kTest, latency.total_count() == 64);
        CRUCIBLE_TEST_ASSERT(kTest, drain.total_count() == 32);

        // Resetting one leaves the other alone, which shared state
        // would not.
        latency.reset();
        CRUCIBLE_TEST_ASSERT(kTest, latency.total_count() == 0);
        CRUCIBLE_TEST_ASSERT(kTest, drain.total_count() == 32);
    }

    check_published_buckets(ReaderStart::WithProducers);
    check_published_buckets(ReaderStart::AfterProducers);

    auto whole = ::foundation::permissions::mint_permission_root<
        ::fixy::concurrent::channel_tag::Whole<::fixy::concurrent::ChannelForm::wire, HistStreamTag>>();
    using Channel = crucible::observe::HdrRecordChannel<2, 1000000, 8, HistStreamTag,
                                                        ::foundation::brand::brand_of_t<decltype(whole)>>;
    Channel channel;
    auto [producer_perm, consumer_perm] =
        ::foundation::permissions::mint_permission_split<typename Channel::producer_tag,
                                                         typename Channel::consumer_tag>(std::move(whole));
    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));

    CRUCIBLE_TEST_ASSERT(kTest, producer.try_push(42));
    CRUCIBLE_TEST_ASSERT(kTest, producer.try_push(84));

    Hist streamed;
    CRUCIBLE_TEST_ASSERT(kTest, crucible::observe::drain_record_stream(streamed, consumer, 8) == 2);
    CRUCIBLE_TEST_ASSERT(kTest, streamed.total_count() == 2);
    CRUCIBLE_TEST_ASSERT(kTest, streamed.percentile(100.0) >= 84);
}
