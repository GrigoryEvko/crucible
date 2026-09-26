#include <crucible/cntp/Pacing.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

// Live fq verification requires a tagged DeclaredQdiscConfig minted by
// the BBR-compatible qdisc gate.  Raw qdisc config values cannot drive
// runtime qdisc policy.  The test runner context passes the socket gate,
// so the raw config is the only reason for the refusal.

int main() {
    ::fixy::TestRunnerCtx test_ctx{::foundation::effects::testing::test()};
    auto iface = crucible::cntp::NicInterfaceName::from("eth0").value();
    crucible::cntp::QdiscConfig raw{
        .interface = iface,
        .required = crucible::cntp::Qdisc::Fq,
        .fq = {},
        .allow_auto_config = false,
    };
    auto result = crucible::cntp::ensure_fq_active(test_ctx, raw);
    (void)result;
    return 0;
}
