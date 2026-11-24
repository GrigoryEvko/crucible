// The body of a scheduling frame keeps the borrow of the proof in a variable
// that it captures by reference.  The proof ends with the frame, so the kept
// borrow would name an object that no longer exists.  The store check of the
// frame (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the
// body.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Sched.h>

namespace {

[[maybe_unused]] void attempt(::fixy::BgDrainCtx const& ctx) {
    ::fixy::Option<::fixy::Ref<fixy::sched::SchedPriority<0> const>> kept{::fixy::none};
    [[maybe_unused]] auto refused = fixy::sched::mint_priority<0>(
        ctx, [&kept](auto proof) noexcept { kept = ::fixy::some(::fixy::erase_brand(::fixy::to(*proof))); });
}

}  // namespace

int main() { return 0; }
