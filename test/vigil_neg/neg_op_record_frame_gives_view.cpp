// Negative-compile test.  This file must fail to compile.
//
// The arm of a frame door of the record stack gives the View of its slots as
// its result.  The frame gives its slots back when the arm returns, so the
// View would name the slots of the next frame.  The result of a frame door
// holds no borrow, so the call binds to the result twin of the window door.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::OpRecordStack stack{};
    const auto kept = stack.with_frame(
        ::foundation::core::WindowCount{1}, [](auto slots) noexcept { return slots.view(); },
        [] noexcept { return ::fixy::View<const crucible::TensorMeta>{}; });
    return static_cast<int>(kept.size());
}
