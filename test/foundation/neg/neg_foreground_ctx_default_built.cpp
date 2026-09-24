// A foreground context is not built from nothing.
//
// A gate that admits only the foreground context states that its caller
// runs on the thread that won the producer claim.  A default constructor
// let any thread make that claim, so the context has none: it is built
// from a foreground source, and the source from the key of the claim.
//
// VIOLATION: a translation unit builds the foreground context with braces.
//
// Expected diagnostic: no matching call to the ExecCtx constructor with no
// argument.

#include <foundation/effects/Ctx.h>

int main() {
    namespace fe = ::foundation::effects;
    fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>> const forged{};
    static_cast<void>(forged);
    return 0;
}
