// A class that copies the shape of a pin is not a pin.  It declares
// is_singleton_pin and a posture, as CpuPinned does, but nothing pinned
// the thread.  IsSingletonCpuPin recognises a CpuPinned by the
// reflection query of foundation/reflect/Instance.h, so the copy is
// refused at the mint, and the TSC reader is never built.
//
// The context and the copy are taken by reference, so nothing here has
// to build them.

#include <fixy/os/Time.h>

#include <utility>

namespace eff = foundation::effects;

namespace forged_pin {

struct CopiedShape {
    static constexpr bool is_singleton_pin = true;
    static constexpr fixy::PinningPosture posture = fixy::PinningPosture::PinnedExplicit;
};

}  // namespace forged_pin

namespace {

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg>>;
using forged_pin::CopiedShape;

[[maybe_unused]] void attempt(BgCtx const& ctx, CopiedShape&& copy) {
    [[maybe_unused]] auto reader = fixy::time::mint_tsc_reader<fixy::time::TscMode::Raw>(ctx, std::move(copy));
}

}  // namespace

int main() { return 0; }
