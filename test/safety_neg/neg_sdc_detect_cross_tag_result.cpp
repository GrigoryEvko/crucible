// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// HS14 fixture #4 for GAPS-180. External provenance is not equivalent to
// SdcVerified provenance even when the underlying value type is identical.

#include <crucible/observe/SdcDetect.h>

namespace safety = crucible::safety;
namespace observe = crucible::observe;

int consume_verified(observe::SdcVerified<std::uint64_t>) noexcept {
    return 0;
}

int main() {
    safety::Tagged<std::uint64_t, safety::source::External> raw{42};
    return consume_verified(raw);
}
