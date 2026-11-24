// Two bodies of run_joined borrow one split, and each takes the token of one
// side by move.  The owner holds a plain count that the two sides share, and
// two threads cannot write a plain count at the same time.  So the split of
// that owner states no shared write, and the door that starts the threads
// refuses the shared borrow.  A split of an owner that orders the state of
// its two sides through cells of the base opens its sides on two threads
// (test_spsc_ends).

#include <fixy/Ctx.h>
#include <fixy/concurrent/SpscEnds.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

using ::fixy::concurrent::ConsumerEnd;
using ::fixy::concurrent::ProducerEnd;

class PlainLog final {
public:
    struct producer_state {};
    struct consumer_state {};

    PlainLog() noexcept = default;
    PlainLog(PlainLog const&) = delete("a log is pinned to its two ends");
    PlainLog(PlainLog&&) = delete("a log is pinned to its two ends");

    static void write(ProducerEnd<PlainLog>& end) noexcept { end.owner_->published_ += 1; }
    [[nodiscard]] static std::uint64_t read(ConsumerEnd<PlainLog>& end) noexcept { return end.owner_->published_; }

private:
    std::uint64_t published_ = 0;
};

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto [split, producer_token, consumer_token] =
        ::fixy::concurrent::mint_spsc_split<PlainLog>(::foundation::effects::testing::test().alloc);
    auto producer = [&split, token = std::move(producer_token)]() mutable noexcept {
        auto back =
            with_producer(split, std::move(token), [](ProducerEnd<PlainLog>& end) noexcept { PlainLog::write(end); });
        static_cast<void>(back);
    };
    auto consumer = [&split, token = std::move(consumer_token)]() mutable noexcept {
        auto [read, back] = with_consumer(split, std::move(token),
                                          [](ConsumerEnd<PlainLog>& end) noexcept { return PlainLog::read(end); });
        static_cast<void>(back);
        return read;
    };
    using Values = ::foundation::core::Record<::foundation::core::Unit, std::uint64_t>;
    using Refused = ::fixy::thread::RefusedStart<decltype(producer), decltype(consumer)>;
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(producer), std::move(consumer))
        .match([](Values) noexcept { return 0; },
               [](Refused refusal) noexcept {
                   auto [producer_body, consumer_body] = std::move(refusal.parts);
                   ::foundation::linear::end_unrun_body(std::move(producer_body),
                                                        ::foundation::linear::UnrunReason::start_refused);
                   ::foundation::linear::end_unrun_body(std::move(consumer_body),
                                                        ::foundation::linear::UnrunReason::start_refused);
                   return 1;
               });
}
