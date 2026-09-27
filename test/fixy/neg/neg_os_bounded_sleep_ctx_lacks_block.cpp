// A sleep blocks the calling thread, so the sleep mint asks for a context
// that owns Block.  A background drain context owns Bg and Alloc and no
// Block, and it is refused.

#include <fixy/Ctx.h>
#include <fixy/os/Time.h>

int main() {
    fixy::BgDrainCtx const ctx{foundation::effects::testing::bg()};
    [[maybe_unused]] auto refused = fixy::time::mint_bounded_sleep<1000>(ctx);
    return 0;
}
