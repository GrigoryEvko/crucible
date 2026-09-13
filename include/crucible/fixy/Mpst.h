#pragma once

// This path forwards to the canonical session-global header. New code
// includes that header directly.

#include <crucible/fixy/SessGlobal.h>

namespace crucible::fixy::sess::mpst::v068_shim_test {

struct ShimProbe {};
using EndAlias_via_shim = ::crucible::fixy::sess::mpst::End_G;
using TransmissionAlias_via_shim =
    ::crucible::fixy::sess::mpst::Transmission<ShimProbe, ShimProbe, ShimProbe, ::crucible::fixy::sess::mpst::End_G>;

static_assert(std::is_same_v<EndAlias_via_shim, ::crucible::safety::proto::End_G>,
              "This path must resolve End_G to the same substrate type as the "
              "canonical header. Re-declaring the types here instead of "
              "forwarding to it breaks that identity.");

}  // namespace crucible::fixy::sess::mpst::v068_shim_test
