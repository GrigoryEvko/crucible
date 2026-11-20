// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A ChannelBinding refers to its channel as a reference does, and the handles
// on several threads use the channel at once.  So a binding moves to another
// thread only when two threads can write its channel (foundation/Transfer.h).
// This class binds its handles as a channel does, but its count is plain.  A
// body beside its caller writes the count through the binding while the
// continuation writes it too, so the door refuses the body.  Rust: a pointer
// that two threads use at once is Send only when its target is Sync.
//
// Expected diagnostic: the verdict of the door names the field and the cause.

#include <fixy/os/ThreadTasks.h>
#include <foundation/ChannelBinding.h>
#include <foundation/effects/Ctx.h>

class PlainChannel {
public:
    // Only the class builds the key of a binding to it.
    [[nodiscard]] ::foundation::ChannelBinding<PlainChannel> bind() noexcept {
        return ::foundation::ChannelBinding<PlainChannel>{*this, ::foundation::BindingKey<PlainChannel>{}};
    }

    int count = 0;
};

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    PlainChannel channel{};
    [[maybe_unused]] auto started = ::fixy::thread::run_beside(
        ctx, ::fixy::thread::ThreadOptions{}, [binding = channel.bind()]() noexcept { ++binding->count; },
        [&channel]() noexcept { ++channel.count; });
    return channel.count;
}
