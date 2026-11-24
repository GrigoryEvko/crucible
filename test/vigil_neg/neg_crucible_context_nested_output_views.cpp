// A body of with_output_views that reaches its context through a capture that
// is not const could open a second frame of the outputs of the op, or end the
// pool.  The frame door of the windows stores no borrow outside the frame, so
// it refuses the capture.

#include <crucible/CrucibleContext.h>

namespace {
[[maybe_unused]] void write_nested(crucible::CrucibleContext& ctx,
                                   crucible::CrucibleContext::CompiledView const& cv) noexcept {
    ctx.with_output_views<float>(
        cv, [&ctx, &cv](auto&) noexcept { return ctx.with_output_views<float>(cv, [](auto&) noexcept {}).is_ok(); });
}
}  // namespace

int main() { return 0; }
