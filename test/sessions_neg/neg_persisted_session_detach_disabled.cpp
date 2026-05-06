#include <crucible/bridges/SessionPersistence.h>

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

struct Resource {};

using P = proto::Send<int, proto::End>;

int main() {
    auto cipher = crucible::Cipher::open("/tmp/crucible_neg_persist_detach");
    eff::TestRunnerCtx ctx{};
    auto h = proto::mint_persisted_session<P>(
        ctx,
        cipher,
        Resource{},
        proto::SessionTagId{1},
        proto::RoleTagId{1},
        proto::RoleTagId{2});

    std::move(h).detach(proto::detach_reason::TestInstrumentation{});
    return 0;
}
