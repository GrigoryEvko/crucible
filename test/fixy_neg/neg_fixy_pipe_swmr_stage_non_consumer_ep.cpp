// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-AUDIT-D7 fixture: `mint_swmr_stage` via fixy:: alias rejects
// when the first (consumer endpoint) parameter is not an
// IsConsumerEndpoint.
//
// Violation: passes a bare int as the consumer endpoint.
// `swmr_stage_from_endpoint_gate::compute()` short-circuits on
// `!IsConsumerEndpoint<consumer_ep>` and returns false →
// `CtxFitsSwmrStageFromEndpoint` fails → requires-clause fires.
//
// Expected diagnostic: "associated constraints are not satisfied"
// pointing at CtxFitsSwmrStageFromEndpoint /
// swmr_stage_from_endpoint_gate / IsConsumerEndpoint.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Pipe.h>
#include <crucible/permissions/Permission.h>

#include <optional>
#include <utility>

namespace eff   = crucible::effects;
namespace fpipe = crucible::fixy::pipe;
namespace conc  = crucible::concurrent;
namespace saf   = crucible::safety;

template <typename T>
struct FakeConsumer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

struct SnapTag {};
using Snapshot = conc::PermissionedSnapshot<int, SnapTag>;

inline void swmr_publish_body(FakeConsumer<int>&&,
                              Snapshot::WriterHandle&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    Snapshot snapshot;
    auto writer_perm = saf::mint_permission_root<Snapshot::writer_tag>();
    auto writer = snapshot.writer(std::move(writer_perm));

    int not_an_endpoint = 0;

    auto bad = fpipe::mint_swmr_stage<&swmr_publish_body>(
        ctx, not_an_endpoint, std::move(writer));
    (void)bad;
    return 0;
}
