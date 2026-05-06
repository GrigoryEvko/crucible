// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-079 follow-up: persisted sessions require a Cipher::OpenView at
// the mint boundary.  A bare Cipher& does not carry the Open-state
// proof needed before the bridge stores a long-lived Cipher reference.

#include <crucible/bridges/SessionPersistence.h>

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

struct Resource {};

int main() {
    auto cipher = crucible::Cipher::open("/tmp/crucible_neg_persist_no_view");
    eff::TestRunnerCtx ctx{};

    [[maybe_unused]] auto h = proto::mint_persisted_session<
        proto::Send<int, proto::End>>(
            ctx,
            cipher,
            Resource{},
            proto::SessionTagId{1},
            proto::RoleTagId{1},
            proto::RoleTagId{2});
    return 0;
}
