// Deviation 4 of fixy/os/Spawn.h: the body takes its shard by mutable
// reference and gives it back, because recombine consumes every shard to
// reissue the parent's Permission.  The old body took the shard by rvalue
// and consumed it, and the parent was then rebuilt by a helper that
// minted a Permission from nothing.
//
// The assertion below is inverted on purpose: the concept must NOT admit a
// body with the old signature.  Asserting through the concept keeps this
// to one diagnostic, where calling the mint with such a body cascades into
// four.

#include <fixy/os/Spawn.h>

namespace eff = foundation::effects;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

// A body in the old shape: it takes the shard by rvalue and consumes it.
// The shard's brand is the region's, so the body is written generic
// over it, which is the shape a body that took the shard by reference
// would also have.
struct ConsumingBody {
    template <class Brand>
    void operator()(fixy::OwnedRegion<int, fixy::Slice<RegionWhole, 0>, Brand>&&) const noexcept {}
};
using RegionBrand = ::foundation::brand::DefaultBrand;
}  // namespace

static_assert(fixy::spawn::CtxFitsParallelFor<2, BgCtx, int, RegionWhole, RegionBrand, ConsumingBody>,
              "a body that consumes its shard must be refused: recombine needs every shard back to reissue "
              "the parent Permission, and the helper that rebuilt the parent from nothing is gone");

int main() { return 0; }
