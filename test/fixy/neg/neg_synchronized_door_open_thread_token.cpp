// Negative compile test.  This file must fail to compile.
//
// The counter states synchronized{} and holds a plain private count.  Its door
// bump takes a key that states thread_bound{} and is a member class of the
// counter, so the key is tied to the counter.  But the key has a public
// constructor, so each thread builds one.  Two bodies of run_joined each build
// a key and call bump at the same time, and the two writes of the count race.
// So the check refuses the key as a proof (the build of a token of
// foundation/Transfer.h).
//
// Expected diagnostic: a door takes a token of the class that each thread can
// build, and the diagnostic names the door bump and the key.

#include <fixy/os/ThreadTasks.h>
#include <foundation/Transfer.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

namespace eff = ::foundation::effects;
using WorkerCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;

constexpr std::uint32_t kRounds = 2000000;

class[[= ::foundation::transfer::synchronized{}]] Counter {
public:
    struct[[= ::foundation::transfer::thread_bound{}]][[= ::foundation::transfer::not_shareable{}]] Key final {};

    Counter() = default;
    Counter(Counter const&) = delete("a count of two threads does not copy");
    Counter& operator=(Counter const&) = delete("a count of two threads does not copy");

    void bump(Key& /*key*/) noexcept { count_ = count_ + 1; }
    [[nodiscard]] std::uint32_t read() const noexcept { return count_; }

private:
    std::uint32_t count_ = 0;
};

}  // namespace

int main() {
    Counter counter{};
    auto first = [&counter]() noexcept {
        for (std::uint32_t round = 0; round < kRounds; ++round) {
            Counter::Key key{};
            counter.bump(key);
        }
    };
    auto second = first;
    ::fixy::thread::run_joined(WorkerCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{}, std::move(first),
                               std::move(second))
        .expect("the probe starts two threads");
    return counter.read() == 2 * kRounds ? 0 : 3;
}
