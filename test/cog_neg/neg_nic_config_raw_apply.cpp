// NEGATIVE-COMPILE TEST. This file MUST FAIL TO COMPILE.
//
// A privileged apply takes a declared configuration. A raw plan never
// passed the mint or its validator, and it does not convert to one.

#include <crucible/cog/NicConfig.h>

// The apply path is a stub, and its deprecation is not the error under
// test.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace nic = crucible::cog::nic;

int main() {
    nic::NicConfigPlan raw{};
    auto result = nic::apply_config(raw);
    return result.has_value() ? 0 : 1;
}
