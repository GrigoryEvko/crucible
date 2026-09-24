// A foreground context is not started over bytes.
//
// The trivial copy constructor makes the context an implicit-lifetime
// type.  The source carries no_start_over_bytes, and the checked lifetime
// start refuses every class that holds a marked member.
//
// VIOLATION: a translation unit starts the lifetime of a foreground
// context over a zeroed buffer.
//
// Expected diagnostic: no matching call to start_as_array, because
// ImplicitLifetimeThroughout is not satisfied.

#include <foundation/Lifetime.h>
#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    alignas(8) unsigned char bytes[8]{};
    auto const forged = ::foundation::lifetime::start_as_array<fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>>(bytes, 1);
    static_cast<void>(forged);
    return 0;
}
