#include <crucible/cntp/dataplane/TcEbpf.h>

int main() {
    crucible::cog::CogIdentity id{};
    crucible::cog::NicPortTargetCaps caps{};
    crucible::cntp::dataplane::TcProgramSpec raw{};
    auto admitted = crucible::cntp::dataplane::tc_admit_nic(id, caps, raw);
    return admitted.has_value() ? 0 : 1;
}
