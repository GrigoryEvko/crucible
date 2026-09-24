#include <crucible/cntp/Pacing.h>

// GAPS-121 fixture #3: live fq verification requires a tagged
// DeclaredQdiscConfig minted by the BBR-compatible qdisc gate. Raw
// qdisc config values cannot drive runtime qdisc policy.

int main() {
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    crucible::cntp::QdiscConfig raw{
        .interface = iface,
        .required = crucible::cntp::Qdisc::Fq,
        .fq = {},
        .allow_auto_config = false,
    };
    auto result = crucible::cntp::ensure_fq_active(raw);
    (void)result;
    return 0;
}
