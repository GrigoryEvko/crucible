// A body that emits through an I/O channel runs only where the
// surrounding context admits that effect.  The foreground context claims
// the empty row — its capability source permits nothing else — so a
// function gated on Row<IO> cannot be called with it, and the refusal is
// at the call site rather than inside the body.
//
// One deviation from the shape this case is usually written in, and it
// is a gap rather than a choice: the gate below spells Row<IO> rather
// than lifting it from the role's own Effect grade.  atom::with<Es...>
// carries no `lifts_to`, so foundation/effects/Lift.h cannot map the
// Effect axis's own atom to the row it names — only the SyscallSurface
// atoms of fixy/atoms/Os.h lift, which is why the os mints can fold a
// pack into a required row and nothing else can.  Until with<Es...>
// lifts, a caller that wants to gate on a binding's declared effects
// writes the row twice: once in the binding, once in the gate.  The
// eight neg_os_*_ctx_lacks_* fixtures cover the lifted form.

#include <fixy/Ctx.h>
#include <fixy/Role.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

namespace {

namespace fe = ::foundation::effects;

template <class Ctx>
    requires fe::CtxAdmits<Ctx, fe::Row<fe::Effect::IO>>
[[nodiscard]] int emit(Ctx const&, ::fixy::role::IoFunction<int> const& bound) noexcept {
    return bound.value();
}

}  // namespace

int main() {
    const auto emitted = ::fixy::mint_fn_for<::fixy::role::IoFunction>(7);
    return emit(::fixy::HotFgCtx{}, emitted);
}
