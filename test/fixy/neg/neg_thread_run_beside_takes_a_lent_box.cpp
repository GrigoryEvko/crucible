// The body of the frame door of a Box holds a reference to the object.  A
// move of the Box into the body of run_beside would give the object to a
// second thread, which writes it while the reference of the frame reads it.
// The frame door carries the annotation borrow_match, so the rule
// borrow-match refuses the move of the Box in the body of the frame.

#include <fixy/Core.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace eff = ::foundation::effects;
using WorkerCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;

int main() {
    ::fixy::Box<std::uint64_t> box = ::fixy::mint_box<std::uint64_t>(eff::testing::test().alloc, std::uint64_t{0});
    auto [seen, back] = ::foundation::core::with_mut(std::move(box), [&box](std::uint64_t& object) noexcept {
        auto writer = [held = std::move(box)]() mutable noexcept { static_cast<void>(held); };
        auto reader = [&object]() noexcept { return object; };
        auto [unit, value] = ::fixy::thread::run_beside(WorkerCtx{eff::testing::bg()}, ::fixy::thread::ThreadOptions{},
                                                        std::move(writer), reader)
                                 .expect("the fixture starts one thread");
        static_cast<void>(unit);
        return value;
    });
    static_cast<void>(back);
    return seen != 0 ? 7 : 0;
}
