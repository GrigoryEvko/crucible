#include <crucible/observe/HdrHistogram.h>

#include <atomic>
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

    Hist delta;
    delta.record(Hist::checked_value(20));
    h.subtract_from(delta);
    assert(h.total_count() == 3);

    Hist merged;
    merged.merge_from(h);
    merged.merge_from(delta);
    assert(merged.total_count() == 4);

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
