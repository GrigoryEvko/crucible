// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// The foreground hot-path context cannot change the pingmesh histograms.
// A foreground context is built only from the producer claim, so the
// refused call names it in an unevaluated operand.  The size of the call
// is its one use, so the refused call is the only error.

#include <crucible/topology/Pingmesh.h>

#include <utility>

int main() {
    auto mesh = crucible::topology::mint_pingmesh<::fixy::ColdInitCtx, 2>(
        ::fixy::ColdInitCtx{::foundation::effects::testing::init()});
    auto const measurement =
        ::fixy::mint_tagged<::fixy::tags::source::Pingmesh>(crucible::topology::PingmeshMeasurement{});

    static_cast<void>(sizeof(mesh.record_measurement(std::declval<::fixy::HotFgCtx const&>(), measurement)));
    return 0;
}
