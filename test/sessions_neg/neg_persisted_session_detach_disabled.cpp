// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-079 follow-up: persisted detach is deliberately disabled until
// RecordingSessionHandle grows a state-owning detach that consumes the
// wrapped inner handle.  The bridge must reject intentional
// abandonment at compile time instead of leaving a debug-only abort
// path.

#include <crucible/bridges/SessionPersistence.h>

namespace proto = crucible::safety::proto;
namespace eff = crucible::effects;

struct Resource {};

using P = proto::Send<int, proto::End>;

int main() {
    auto cipher = crucible::Cipher::open("/tmp/crucible_neg_persist_detach");
    auto view = cipher.mint_open_view();
    eff::TestRunnerCtx ctx{};
    auto h = proto::mint_persisted_session<P>(
        ctx,
        cipher,
        view,
        Resource{},
        proto::SessionTagId{1},
        proto::RoleTagId{1},
        proto::RoleTagId{2});

    std::move(h).detach(proto::detach_reason::TestInstrumentation{});
    return 0;
}
