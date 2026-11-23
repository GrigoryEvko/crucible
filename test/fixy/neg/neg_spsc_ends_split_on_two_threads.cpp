// Two bodies of run_joined borrow one split, and each opens one side with the
// key of that side.  A split stays on one thread: its doors take loan keys,
// which prove no thread, so the split states no shared write, and the door
// that starts the threads refuses the shared borrow.  The owner orders the
// count that its two sides share through a cell of the base, so the refusal
// comes from the split and not from the owner.
#include <fixy/Ctx.h>
#include <fixy/concurrent/SpscEnds.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Atomic.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

using ::fixy::concurrent::ConsumerEnd;
using ::fixy::concurrent::ProducerEnd;

class CellLog final {
public:
    struct producer_state {};
    struct consumer_state {};

    CellLog() noexcept = default;
    CellLog(CellLog const&) = delete("a log is pinned to its two ends");
    CellLog(CellLog&&) = delete("a log is pinned to its two ends");

    static void write(ProducerEnd<CellLog>& end) noexcept {
        static_cast<void>(end.owner_->published_.fetch_add_relaxed(1));
    }
    static void read(ConsumerEnd<CellLog>& end) noexcept { static_cast<void>(end.owner_->published_.load_acquire()); }

private:
    ::foundation::core::Atomic<std::uint64_t, ::foundation::core::Counter> published_{std::uint64_t{0}};
};

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    auto [split, producer_key, consumer_key] =
        ::fixy::concurrent::mint_spsc_split<CellLog>(::foundation::effects::testing::test().alloc);
    auto producer = [&split, &producer_key]() noexcept {
        split.with_producer(producer_key, [](ProducerEnd<CellLog>& end) noexcept { CellLog::write(end); });
    };
    auto consumer = [&split, &consumer_key]() noexcept {
        split.with_consumer(consumer_key, [](ConsumerEnd<CellLog>& end) noexcept { CellLog::read(end); });
    };
    return ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(producer), std::move(consumer))
                   .is_ok()
             ? 0
             : 1;
}
