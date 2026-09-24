// A foreground context is not built from a byte.
//
// The source keeps a trivial copy constructor, so the hot path passes it
// in no register.  Its copy assignment is user-provided, so the context
// is not trivially copyable, and std::bit_cast refuses it.
//
// VIOLATION: a translation unit reinterprets one byte as a foreground
// context.
//
// Expected diagnostic: no matching call to bit_cast, because the target
// is not trivially copyable.

#include <foundation/effects/Ctx.h>

#include <bit>

int main() {
    namespace fe = ::foundation::effects;
    auto const forged = std::bit_cast<fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>>(char{0});
    static_cast<void>(forged);
    return 0;
}
