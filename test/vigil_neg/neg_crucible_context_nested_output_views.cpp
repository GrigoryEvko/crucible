// A body of with_output_views that reaches its compiled scope through a capture
// could open a second frame of the outputs of the op.  The scope names its
// context, which a write could change, so the frame door of the windows, which
// stores no borrow outside the frame, refuses the capture.

#include <crucible/CrucibleContext.h>

namespace {
[[maybe_unused]] void write_nested(crucible::CompiledScope& scope) noexcept {
    static_cast<void>(scope.with_output_views<float>(
        [&scope](auto&) noexcept { return scope.with_output_views<float>([](auto&) noexcept {}).is_ok(); }));
}
}  // namespace

int main() { return 0; }
