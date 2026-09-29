// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A raw operation result cannot stand in for the SdcVerified provenance
// tag that only a completed redundant check mints.

#include <crucible/observe/SdcDetect.h>

#include <cstdint>

namespace observe = crucible::observe;

static int consume_verified(observe::SdcVerified<std::uint64_t>) noexcept { return 0; }

int main() { return consume_verified(std::uint64_t{42}); }
