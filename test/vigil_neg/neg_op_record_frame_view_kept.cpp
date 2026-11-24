// Negative-compile test.  This file must fail to compile.
//
// The arm of a frame door of the record stack keeps the View of its slots in
// a variable that it captures by reference.  The frame gives its slots back
// when the arm returns, and the next op writes them, so the View would read
// the records of another op.  The store check of the window door refuses the
// arm.

#include <crucible/ValidatedOp.h>

int main() {
    crucible::OpRecordStack stack{};
    ::fixy::Option<::fixy::View<const crucible::TensorMeta>> kept{::fixy::none};
    stack.with_frame(
        ::foundation::core::WindowCount{1},
        [&kept](auto slots) noexcept {
            kept = ::fixy::Option<::fixy::View<const crucible::TensorMeta>>::some(slots.view());
        },
        [] noexcept {});
    return kept.is_some() ? 0 : 1;
}
