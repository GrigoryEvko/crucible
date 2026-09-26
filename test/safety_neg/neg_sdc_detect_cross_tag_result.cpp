// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// External provenance is not SdcVerified provenance, even when the
// value type is the same.

#include <crucible/observe/SdcDetect.h>

#include <cstdint>

namespace observe = crucible::observe;

static int consume_verified(observe::SdcVerified<std::uint64_t>) noexcept { return 0; }

int main() {
    auto raw = ::fixy::mint_tagged<::fixy::tags::source::External>(std::uint64_t{42});
    return consume_verified(raw);
}
