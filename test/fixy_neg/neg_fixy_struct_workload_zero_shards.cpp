// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-Workload fixture #1: parallel_for_views<N=0> via the
// fixy:: alias rejects N==0 with the substrate's static_assert.
//
// Violation: parallel_for_views<N> static_asserts `N > 0`.  Passing
// 0 fires the assertion.  Routing through
// `fixy::struct_::parallel_for_views` must reject identically.
//
// Expected diagnostic: substring "requires N > 0" / "static_assert".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Struct.h>
#include <crucible/permissions/Permission.h>

#include <utility>

namespace fstr = crucible::fixy::struct_;
namespace saf  = crucible::safety;

namespace neg_fixy_struct_workload_zero_shards {

struct TypeStructWorkloadZeroShards {};

}  // namespace neg_fixy_struct_workload_zero_shards

int main() {
    namespace tags = neg_fixy_struct_workload_zero_shards;
    using Region = fstr::OwnedRegion<int, tags::TypeStructWorkloadZeroShards>;

    auto perm = saf::mint_permission_root<tags::TypeStructWorkloadZeroShards>();
    int storage[4] = {0, 0, 0, 0};
    Region r = Region::wrap(storage, 4, std::move(perm));

    auto body = [](auto&&) noexcept {};

    // Should FAIL: N==0 violates `parallel_for_views<N> requires N > 0`.
    [[maybe_unused]] auto rebuilt =
        fstr::parallel_for_views<0>(std::move(r), body);
    return 0;
}
