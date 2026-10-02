#pragma once

// A header of the layer low of the test table.  Its include of a header of
// the layer high is an upward include.  When the unit entered High.h before,
// #pragma once stops this include, and the plugin finds the target among the
// files that the unit entered.

#include <fixy/high/High.h>

namespace probe {

inline int low_value() { return high_value() - 1; }

}  // namespace probe
