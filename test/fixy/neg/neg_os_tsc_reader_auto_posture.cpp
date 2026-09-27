// An auto pin is best-effort, and the thread can still migrate.  The
// counter belongs to one core, so a read through an auto pin can mix two
// counters.  The TSC mint admits only the explicit posture.

#include <fixy/os/Time.h>

#include <utility>

namespace eff = foundation::effects;
namespace ml = foundation::algebra::lattices;

namespace {

using InitCtx = eff::ExecCtx<eff::Init, eff::Row<eff::Effect::Init>>;
using AutoPin = fixy::CpuPinned<ml::AffinityMask::single(0), fixy::PinningPosture::PinnedAuto, int>;

// The pin arrives as a parameter.  Only mint_affinity builds one.
[[maybe_unused]] void attempt(InitCtx const& ctx, AutoPin&& pin) {
    [[maybe_unused]] auto reader = fixy::time::mint_tsc_reader<fixy::time::TscMode::Raw>(ctx, std::move(pin));
}

}  // namespace

int main() { return 0; }
