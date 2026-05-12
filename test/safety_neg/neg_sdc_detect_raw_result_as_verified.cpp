// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 for GAPS-180. Raw operation results cannot substitute for
// the post-redundancy SdcVerified provenance tag.

#include <crucible/observe/SdcDetect.h>

namespace observe = crucible::observe;

int consume_verified(observe::SdcVerified<std::uint64_t>) noexcept {
    return 0;
}

int main() {
    return consume_verified(std::uint64_t{42});
}
