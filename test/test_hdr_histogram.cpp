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

    // fixy-A5-008 regression: zero is a first-class sample value.  Pre-fix
    // the value_type predicate `in_range<1, MaxValue>` rejected zero by
    // firing Refined<>'s contract — aborting the Keeper daemon the moment
    // any cold-path transport emitted its first sample (no-RTT-yet on a
    // fresh socket, no-bytes-yet on an idle fd, zero-queue-depth on
    // load-shed paths).  After widening to `in_range<0, MaxValue>`:
    //   (a) construction succeeds (no contract violation),
    //   (b) the sample lands in bucket 0 (counts_index(0) == 0),
    //   (c) percentile(100.0) reports a value ≤ existing max — i.e. the
    //       zero sample does not steal probability mass from positive
    //       samples already recorded.
    {
        Hist zero_h;
        zero_h.record(Hist::checked_value(0));
        assert(zero_h.total_count() == 1);
        assert(zero_h.percentile(100.0) == 0);
        assert(zero_h.mean() == 0);

        // Recording zero alongside positive samples must not perturb
        // their bucket counts — the zero sample contributes count 1 to
        // bucket 0 and that's all.
        Hist mixed;
        mixed.record(Hist::checked_value(0));
        mixed.record(Hist::checked_value(0));
        mixed.record(Hist::checked_value(100));
        assert(mixed.total_count() == 3);
        // Two-thirds of mass is at 0; p50 lands in bucket 0.
        assert(mixed.percentile(50.0) == 0);
        // The lone positive sample reaches p100.
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

    // fixy-A5-019 regression: subtract_from must publish bucket writes
    // via release on total_count_ matching merge_from's discipline.  A
    // consumer that reads total_count() via acquire must then see the
    // post-subtract bucket state consistently — i.e. percentile reads
    // never observe a "total decreased but buckets unchanged" tear.
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
        // Reader-side: load total_count() (acquire), then read bucket
        // state via percentile().  If subtract_from's publish discipline
        // works, the bucket sum must equal the post-subtract total.
        const std::uint64_t post_total = publish_h.total_count();
        assert(post_total == 12);
        std::uint64_t bucket_sum = 0;
        publish_h.for_each_nonzero(
            [&](const Hist::EncodedBucket& bucket) noexcept {
                bucket_sum += bucket.count;
            });
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

    using Channel = crucible::observe::HdrRecordChannel<2, 1'000'000, 8, HistStreamTag>;
    Channel channel;
    auto whole = crucible::safety::mint_permission_root<typename Channel::whole_tag>();
    auto [producer_perm, consumer_perm] =
        crucible::safety::mint_permission_split<
            typename Channel::producer_tag,
            typename Channel::consumer_tag>(std::move(whole));
    auto producer = channel.producer(std::move(producer_perm));
    auto consumer = channel.consumer(std::move(consumer_perm));

    assert(producer.try_push(42));
    assert(producer.try_push(84));

    Hist streamed;
    assert(crucible::observe::drain_record_stream(streamed, consumer, 8) == 2);
    assert(streamed.total_count() == 2);
    assert(streamed.percentile(100.0) >= 84);
}
