// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// An epoch wrapper over the foreground context, built by std::bit_cast from
// bytes of the same size.  The wrapper is not trivially copyable, so the
// constraint of std::bit_cast refuses it, and a claim about the session epoch
// cannot come from bytes.

#include <crucible/sessions/SessionMint.h>

#include <bit>

namespace eff = ::crucible::effects;
namespace proto = ::crucible::safety::proto;

namespace {
using Forged = proto::EpochExecCtx<9, 9, eff::HotFgCtx>;
struct SameSizeBytes {
    unsigned char bytes[sizeof(Forged)]{};
};
}  // namespace

int main() {
    const auto forged = std::bit_cast<Forged>(SameSizeBytes{});
    (void)forged;
    return 0;
}
