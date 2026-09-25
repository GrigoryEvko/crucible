// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// `mint_swmr_stage` rejects a call when the first parameter (the consumer
// endpoint) does not satisfy IsConsumerEndpoint.
//
// Violation: a bare int is the consumer endpoint.
// `swmr_stage_from_endpoint_gate::compute()` stops at
// `!IsConsumerEndpoint<consumer_ep>` and returns false.  Then
// `CtxFitsSwmrStageFromEndpoint` is false, and the requires clause rejects
// the call.
//
// Expected diagnostic: "associated constraints are not satisfied"
// at CtxFitsSwmrStageFromEndpoint, swmr_stage_from_endpoint_gate or
// IsConsumerEndpoint.

#include <crucible/concurrent/PermissionedSnapshot.h>
#include <crucible/concurrent/StageEndpointBridge.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/permissions/_Permission.h>

#include <optional>
#include <utility>

namespace eff = crucible::effects;
namespace conc = crucible::concurrent;
namespace saf = crucible::safety;

template <typename T>
struct FakeConsumer {
    static constexpr std::size_t per_call_working_set = 64;
    [[nodiscard]] std::optional<T> try_pop() noexcept { return {}; }
};

struct SnapTag {};
using Snapshot = conc::PermissionedSnapshot<int, SnapTag>;

inline void swmr_publish_body(FakeConsumer<int>&&, Snapshot::WriterHandle&&) noexcept {}

int main() {
    eff::HotFgCtx ctx;

    Snapshot snapshot;
    auto writer_perm = saf::mint_permission_root<Snapshot::writer_tag>();
    auto writer = snapshot.writer(std::move(writer_perm));

    int not_an_endpoint = 0;

    auto bad = conc::mint_swmr_stage<&swmr_publish_body>(ctx, not_an_endpoint, std::move(writer));
    (void)bad;
    return 0;
}
