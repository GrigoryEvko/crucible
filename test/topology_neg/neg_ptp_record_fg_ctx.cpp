// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The foreground hot-path context cannot change the status or the
// timestamp of a PTP handle.  A foreground context is built only from the
// producer claim, so the refused call names it in an unevaluated operand.

#include <crucible/topology/Ptp.h>

#include <cstdint>
#include <utility>

int main() {
    crucible::cog::CogIdentity nic{};
    nic.uuid = crucible::cog::Uuid{0x129, 2};
    nic.kind = crucible::cog::CogKind::NicPort;
    auto fd = crucible::topology::admit_ptp_clock_fd(5).value();
    ::fixy::ColdInitCtx init{::foundation::effects::testing::init()};
    auto handle = crucible::topology::mint_ptp_handle(init, nic, fd);
    auto const stamp = ::fixy::mint_tagged<::fixy::tags::source::Ptp>(std::uint64_t{7});
    return sizeof(handle.record_timestamp(std::declval<::fixy::HotFgCtx const&>(), stamp, 1), 0) == 0 ? 1 : 0;
}
