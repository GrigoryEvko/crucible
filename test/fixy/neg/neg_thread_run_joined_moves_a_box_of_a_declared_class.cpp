// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A body of run_joined owns a class that holds a Box of a class that this
// unit only declares.  The unit that defines that class can give it a member
// of one owner thread, so this unit cannot know if the Box moves to another
// thread.  A Box owns the object of its argument, so an incomplete argument
// of it is not Sendable (foundation/Transfer.h, rule 6), and the door refuses
// the body (foundation/Closure.h, rule 5).
//
// Expected diagnostic: the verdict of the door names the field and the
// declared class.

#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>

struct DeviceState;

class Device {
public:
    Device(Device&&) noexcept;
    Device& operator=(Device&&) noexcept;
    ~Device();
    void poll() noexcept;

private:
    ::foundation::core::Box<DeviceState> state_;
};

Device open_device() noexcept;

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    auto poll = [device = open_device()]() mutable noexcept { device.poll(); };
    auto idle = []() noexcept {};
    [[maybe_unused]] auto started =
        ::fixy::thread::run_joined(ctx, ::fixy::thread::ThreadOptions{}, std::move(poll), std::move(idle));
    return 0;
}
