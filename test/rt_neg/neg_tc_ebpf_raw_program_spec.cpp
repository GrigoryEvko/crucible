#include <crucible/rt/TcEbpf.h>

int main() {
    crucible::cog::CogIdentity id{};
    crucible::cog::NicPortTargetCaps caps{};
    crucible::rt::TcProgramSpec raw{};
    auto admitted = crucible::rt::tc_admit_nic(id, caps, raw);
    return admitted.has_value() ? 0 : 1;
}
