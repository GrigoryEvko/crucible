// A context without IO refuses a binding that writes to stdio.
//
// A stdio write lifts Row<IO, Block>, because the write takes the lock of
// the stream and a flush can park the caller.  The binding states no
// Effect atom, so the row it requires is the lift alone.  The context here
// holds Bg and Block but not IO, so the gate refuses the call at the call
// site, for the one effect the context lacks.  The binding names
// as_public, so the corpus has no classified channel to refuse and the
// gate is the only refusal.
//
// test/fixy/test_fn.cpp holds the positive control: the load context,
// which holds IO and Block, admits the same binding.

#include <fixy/Fn.h>
#include <fixy/atoms/Stdio.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace {

namespace fe = ::foundation::effects;

using StderrWrite = ::fixy::atom::stdio::write<::fixy::atom::stdio::streams::Stderr>;
using StderrBinding = ::fixy::fn<int, StderrWrite, ::fixy::atom::as_public>;
using BlockOnlyCtx = fe::ExecCtx<fe::Bg, fe::Row<fe::Effect::Bg, fe::Effect::Block>>;

// The row is not written here.  It is read off the binding.
template <class Ctx, class Bound>
    requires ::fixy::CtxAdmitsBinding<Ctx, Bound>
[[nodiscard]] int report(Ctx const&, Bound const& bound) noexcept {
    return bound.value();
}

static_assert(std::is_same_v<::fixy::binding_row_t<StderrBinding>, fe::Row<fe::Effect::IO, fe::Effect::Block>>);

}  // namespace

int main() {
    BlockOnlyCtx const waiter{fe::testing::bg()};
    const auto bound = ::fixy::mint_fn<int, StderrWrite, ::fixy::atom::as_public>(9);
    return report(waiter, bound);
}
