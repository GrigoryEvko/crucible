// The ends of an SPSC owner (fixy/concurrent/SpscEnds.h): the mint builds the
// owner, the split that holds it with its two ends, and the key of each side.
// A body borrows an end through a frame door of the split, and the side
// operations take the end.

#include <fixy/concurrent/SpscEnds.h>

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::concurrent::ConsumerEnd;
using ::fixy::concurrent::ProducerEnd;
using ::fixy::concurrent::SpscSide;

// A log of counts with one producer and one consumer.  The producer state is
// the count that the producer wrote, and the consumer state is the count that
// the consumer read.  The owner keeps only the published counts.
class CountLog final {
public:
    struct producer_state {
        std::uint64_t written = 0;
    };
    struct consumer_state {
        std::uint64_t read = 0;
    };

    explicit CountLog(std::uint64_t capacity) noexcept : capacity_{capacity} {}
    CountLog(CountLog const&) = delete("a log is pinned to its two ends");
    CountLog(CountLog&&) = delete("a log is pinned to its two ends");
    CountLog& operator=(CountLog const&) = delete("a log is pinned to its two ends");
    CountLog& operator=(CountLog&&) = delete("a log is pinned to its two ends");
    ~CountLog() = default;

    // Writes one item, or gives false when the log is full.
    [[nodiscard]] static bool try_write(ProducerEnd<CountLog>& end) noexcept {
        CountLog& log = *end.owner_;
        if (end.state_.written - log.published_read_ >= log.capacity_) return false;
        end.state_.written += 1;
        log.published_written_ = end.state_.written;
        return true;
    }

    // Reads one item, or gives false when the log is empty.
    [[nodiscard]] static bool try_read(ConsumerEnd<CountLog>& end) noexcept {
        CountLog& log = *end.owner_;
        if (end.state_.read == log.published_written_) return false;
        end.state_.read += 1;
        log.published_read_ = end.state_.read;
        return true;
    }

    // The count of items of the log that the two published counts give.
    [[nodiscard]] std::uint64_t held() const noexcept { return published_written_ - published_read_; }

    // True when the two ends name this log.
    [[nodiscard]] bool is_owner_of(ProducerEnd<CountLog> const& producer,
                                   ConsumerEnd<CountLog> const& consumer) const noexcept {
        return ::fixy::same_object(producer.owner_, ::fixy::to(*this))
            && ::fixy::same_object(consumer.owner_, ::fixy::to(*this));
    }

private:
    std::uint64_t capacity_ = 0;
    std::uint64_t published_written_ = 0;
    std::uint64_t published_read_ = 0;
};

// An end is a Ref of its owner and the state of its side.
using EndRef = ::fixy::Ref<CountLog, ::fixy::concurrent::SpscSplitBrand<CountLog>>;
static_assert(sizeof(ProducerEnd<CountLog>) == sizeof(EndRef) + sizeof(CountLog::producer_state));
static_assert(sizeof(ConsumerEnd<CountLog>) == sizeof(EndRef) + sizeof(CountLog::consumer_state));
static_assert(ProducerEnd<CountLog>::side == SpscSide::producer && ConsumerEnd<CountLog>::side == SpscSide::consumer);
static_assert(std::is_same_v<ConsumerEnd<CountLog>::state_type, CountLog::consumer_state>);

// The places of the ends of an owner and of the states of its two ends: each
// end takes the next place.  A test reads them around the end of a split.
int g_end_place = 0;
int g_owner_end_place = 0;
int g_producer_end_place = 0;
int g_consumer_end_place = 0;

// An owner whose end and whose two states record their places.
class OrderLog final {
public:
    struct producer_state {
        ~producer_state() { g_producer_end_place = ++g_end_place; }
    };
    struct consumer_state {
        ~consumer_state() { g_consumer_end_place = ++g_end_place; }
    };

    OrderLog() noexcept = default;
    OrderLog(OrderLog const&) = delete("a log is pinned to its two ends");
    OrderLog(OrderLog&&) = delete("a log is pinned to its two ends");
    ~OrderLog() { g_owner_end_place = ++g_end_place; }
};

// THE SPLIT holds the Box first, so its two ends end before the owner that
// their Ref names: the consumer end, then the producer end, then the owner.
// The order is in the type of the split, and no holder can change it.
int check_the_ends_end_before_the_owner() {
    {
        [[maybe_unused]] auto minted =
            ::fixy::concurrent::mint_spsc_split<OrderLog>(::foundation::effects::testing::test().alloc);
    }
    if (g_consumer_end_place != 1 || g_producer_end_place != 2) return 1;
    if (g_owner_end_place != 3) return 2;
    return 0;
}

// Reads each item that the log holds, through the consumer end.
[[nodiscard]] std::uint64_t read_all(ConsumerEnd<CountLog>& end) noexcept {
    std::uint64_t count = 0;
    while (CountLog::try_read(end))
        ++count;
    return count;
}

// The held count of the log of a split.
template <class Split>
[[nodiscard]] std::uint64_t held_of(Split const& split) noexcept {
    return split.with_owner([](CountLog const& log) noexcept { return log.held(); });
}

int check_split() {
    auto [log, producer_key, consumer_key] =
        ::fixy::concurrent::mint_spsc_split<CountLog>(::foundation::effects::testing::test().alloc, std::uint64_t{3});

    // The two ends name the log of the split.
    bool const is_owner =
        log.with_ends(producer_key, consumer_key,
                      [&log](ProducerEnd<CountLog> const& producer, ConsumerEnd<CountLog> const& consumer) noexcept {
                          return log.with_owner([&producer, &consumer](CountLog const& owner) noexcept {
                              return owner.is_owner_of(producer, consumer);
                          });
                      });
    if (!is_owner) return 1;

    // The producer end keeps its count, and the log keeps the published
    // count.  The borrow form of the door keeps the key with the caller.
    std::uint64_t const written = log.with_producer(producer_key, [](ProducerEnd<CountLog>& end) noexcept {
        std::uint64_t count = 0;
        while (CountLog::try_write(end))
            ++count;
        return count;
    });
    if (written != 3) return 2;
    if (held_of(log) != 3) return 3;

    // The move form gives the key back after the result.
    auto [read, consumer_back] =
        log.with_consumer(std::move(consumer_key), [](ConsumerEnd<CountLog>& end) noexcept { return read_all(end); });
    if (read != 3 || held_of(log) != 0) return 4;

    // A body that gives void gets the key back alone, and the end keeps its
    // state across two frames.
    producer_key = log.with_producer(std::move(producer_key), [](ProducerEnd<CountLog>& end) noexcept {
        static_cast<void>(CountLog::try_write(end));
    });
    if (held_of(log) != 1) return 5;
    if (log.with_consumer(consumer_back, [](ConsumerEnd<CountLog>& end) noexcept { return read_all(end); }) != 1) {
        return 6;
    }
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_split(); rc != 0) return rc;
    if (int rc = check_the_ends_end_before_the_owner(); rc != 0) return 10 + rc;
    return 0;
}
