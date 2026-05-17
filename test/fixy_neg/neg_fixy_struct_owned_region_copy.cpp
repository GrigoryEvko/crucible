// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-STRUCT-OwnedRegion fixture #1: OwnedRegion<T, Tag> via the
// fixy:: alias rejects copy construction.
//
// Violation: OwnedRegion is move-only by virtue of its embedded
// Permission<Tag> (linear).  Copying duplicates exclusive ownership.
// Routing through `fixy::struct_::OwnedRegion` must preserve the
// `= delete` identically.
//
// Expected diagnostic: substring "deleted function" / "copy" /
// "Permission".

#include <crucible/effects/ExecCtx.h>
#include <crucible/fixy/Struct.h>
#include <crucible/permissions/Permission.h>

#include <utility>

namespace fstr = crucible::fixy::struct_;
namespace saf  = crucible::safety;
namespace eff  = crucible::effects;

namespace neg_fixy_struct_owned_region_copy {

struct TypeStructOwnedRegionCopy {};

}  // namespace neg_fixy_struct_owned_region_copy

int main() {
    namespace tags = neg_fixy_struct_owned_region_copy;
    using Region = fstr::OwnedRegion<int, tags::TypeStructOwnedRegionCopy>;

    auto perm = saf::mint_permission_root<tags::TypeStructOwnedRegionCopy>();
    int storage[4] = {0, 0, 0, 0};
    Region a = Region::wrap(storage, 4, std::move(perm));

    // Should FAIL: OwnedRegion is move-only (Permission is linear).
    Region b = a;
    (void)b;
    return 0;
}
