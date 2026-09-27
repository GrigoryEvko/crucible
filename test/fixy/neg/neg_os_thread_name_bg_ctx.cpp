// Naming writes the entry of the calling thread under /proc, and a
// thread is named when it starts.  So the name mint asks for an init
// context.  A background context is refused.

#include <fixy/os/ThreadName.h>

namespace eff = foundation::effects;

namespace {
using BgDrainCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;
}  // namespace

int main() {
    BgDrainCtx const ctx{eff::testing::bg()};
    [[maybe_unused]] auto const& refused = fixy::mint_thread_name<"crux-bg">(ctx);
    return 0;
}
