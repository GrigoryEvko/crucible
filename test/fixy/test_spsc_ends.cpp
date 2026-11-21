// The ends of an SPSC owner (fixy/concurrent/SpscEnds.h): the mint builds the
// owner and its two ends in one call, each end holds the Ref of its owner and
// the state of its side, and the side operations take the end.

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

// An end is a Ref of its owner and the state of its side.  It only moves.
using EndRef = ::fixy::Ref<CountLog, ::fixy::concurrent::SpscSplitBrand<CountLog>>;
static_assert(sizeof(ProducerEnd<CountLog>) == sizeof(EndRef) + sizeof(CountLog::producer_state));
static_assert(sizeof(ConsumerEnd<CountLog>) == sizeof(EndRef) + sizeof(CountLog::consumer_state));
static_assert(!std::is_copy_constructible_v<ProducerEnd<CountLog>>);
static_assert(!std::is_copy_assignable_v<ProducerEnd<CountLog>>);
static_assert(!std::is_move_assignable_v<ProducerEnd<CountLog>>);
static_assert(std::is_nothrow_move_constructible_v<ProducerEnd<CountLog>>);
static_assert(!std::is_constructible_v<ProducerEnd<CountLog>, EndRef>);
static_assert(ProducerEnd<CountLog>::side == SpscSide::producer && ConsumerEnd<CountLog>::side == SpscSide::consumer);
static_assert(std::is_same_v<ConsumerEnd<CountLog>::state_type, CountLog::consumer_state>);

// A holder of one end, as a stage of a pipeline holds the consumer end.
struct ReaderStage final {
    ConsumerEnd<CountLog> end;

    [[nodiscard]] std::uint64_t read_all() noexcept {
        std::uint64_t count = 0;
        while (CountLog::try_read(end))
            ++count;
        return count;
    }
};

int check_split() {
    auto [log, producer, consumer] =
        ::fixy::concurrent::mint_spsc_split<CountLog>(::foundation::effects::testing::test().alloc, std::uint64_t{3});
    if (!log->is_owner_of(producer, consumer)) return 1;

    // The producer end keeps its count, and the log keeps the published count.
    if (!CountLog::try_write(producer) || !CountLog::try_write(producer) || !CountLog::try_write(producer)) return 2;
    if (CountLog::try_write(producer)) return 3;
    if (log->held() != 3) return 4;

    // The consumer end moves into its holder and still names the log.
    ReaderStage stage{.end = std::move(consumer)};
    if (stage.read_all() != 3) return 5;
    if (log->held() != 0) return 6;

    // The producer end moves too, and its state moves with it.
    ProducerEnd<CountLog> moved = std::move(producer);
    if (!CountLog::try_write(moved)) return 7;
    if (log->held() != 1 || stage.read_all() != 1) return 8;
    if (!log->is_owner_of(moved, stage.end)) return 9;
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_split(); rc != 0) return rc;
    return 0;
}
