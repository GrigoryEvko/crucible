// Negative compile test.  This file must fail to compile.
//
// A class holds a Box of a class that this unit only declares.  The unit that
// defines that class can give it a member of one owner thread, so this unit
// cannot know if the Box moves to another thread.  A Box owns the object of
// its argument, so an incomplete argument of it holds no property
// (foundation/Transfer.h, rule 6).  run_beside then refuses a body that owns
// such a class (foundation/Closure.h, rule 8).
//
// Expected diagnostic: the verdict of the door names the field and the
// declared class.

#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>

struct EngineImpl;

class Engine {
public:
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    ~Engine();
    void step() noexcept;

private:
    ::foundation::core::Box<EngineImpl> impl_;
};

Engine make_engine() noexcept;

int main() {
    namespace eff = ::foundation::effects;
    eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>> const ctx{eff::testing::bg()};
    auto body = [engine = make_engine()]() mutable noexcept { engine.step(); };
    auto continuation = []() noexcept {};
    [[maybe_unused]] auto started = ::fixy::thread::run_beside(ctx, ::fixy::thread::ThreadOptions{},
                                                               static_cast<decltype(body)&&>(body), continuation);
    return 0;
}
